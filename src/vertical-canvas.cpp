#include "vertical-canvas.hpp"

#include <util/base.h>
#include <util/platform.h>
#include <media-io/video-io.h>
#include <media-io/video-frame.h>
#include <graphics/vec4.h>

#include <algorithm>
#include <cstring>

#define VC_LOG(level, msg) blog(level, "[vertical-canvas-lite] %s", msg)

VerticalCanvas::~VerticalCanvas()
{
	Shutdown();
}

/* ------------------------------------------------------------------ */
/* Recursos de video                                                   */
/* ------------------------------------------------------------------ */

bool VerticalCanvas::CreateResources(uint32_t w, uint32_t h)
{
	obs_video_info ovi;
	if (!obs_get_video_info(&ovi))
		return false;

	video_output_info vi = {};
	vi.name = "vertical-canvas-lite";
	vi.format = VIDEO_FORMAT_BGRA;
	vi.fps_num = ovi.fps_num;
	vi.fps_den = ovi.fps_den;
	vi.width = w;
	vi.height = h;
	vi.cache_size = 6;
	vi.colorspace = VIDEO_CS_709;
	vi.range = VIDEO_RANGE_PARTIAL;

	video_t *v = nullptr;
	if (video_output_open(&v, &vi) != VIDEO_OUTPUT_SUCCESS)
		return false;

	obs_enter_graphics();
	gs_texrender_t *tr = gs_texrender_create(GS_BGRA, GS_ZS_NONE);
	gs_stagesurf_t *ss = gs_stagesurface_create(w, h, GS_BGRA);
	obs_leave_graphics();

	if (!tr || !ss) {
		obs_enter_graphics();
		if (tr)
			gs_texrender_destroy(tr);
		if (ss)
			gs_stagesurface_destroy(ss);
		obs_leave_graphics();
		video_output_close(v);
		return false;
	}

	video = v;
	texrender = tr;
	stagesurf = ss;
	width = w;
	height = h;
	return true;
}

void VerticalCanvas::DestroyResources()
{
	obs_enter_graphics();
	if (texrender) {
		gs_texrender_destroy(texrender);
		texrender = nullptr;
	}
	if (stagesurf) {
		gs_stagesurface_destroy(stagesurf);
		stagesurf = nullptr;
	}
	obs_leave_graphics();

	if (video) {
		video_output_close(video);
		video = nullptr;
	}
}

bool VerticalCanvas::EnsureInit(uint32_t w, uint32_t h)
{
	if (initialized)
		return true;
	if (!CreateResources(w, h)) {
		VC_LOG(LOG_ERROR, "falha ao criar recursos de video");
		return false;
	}
	initialized = true;
	obs_add_main_render_callback(RenderCallback, this);
	return true;
}

bool VerticalCanvas::Reconfigure(uint32_t w, uint32_t h)
{
	if (!initialized)
		return EnsureInit(w, h);
	if (Busy())
		return false;

	const uint32_t oldW = width, oldH = height;

	obs_remove_main_render_callback(RenderCallback, this);
	DestroyResources();

	bool ok = CreateResources(w, h);
	if (!ok)
		ok = CreateResources(oldW, oldH) && false;

	obs_add_main_render_callback(RenderCallback, this);
	return ok;
}

void VerticalCanvas::Shutdown()
{
	if (!initialized)
		return;

	StopAllAndWait();
	obs_remove_main_render_callback(RenderCallback, this);
	SetScene(nullptr);
	DestroyResources();
	initialized = false;
}

/* ------------------------------------------------------------------ */
/* Cena                                                                */
/* ------------------------------------------------------------------ */

void VerticalCanvas::SetScene(obs_source_t *s)
{
	obs_source_t *ref = s ? obs_source_get_ref(s) : nullptr;
	if (ref) {
		obs_source_inc_showing(ref);
		obs_source_inc_active(ref);
	}

	obs_source_t *old = nullptr;
	{
		std::lock_guard<std::mutex> lock(sceneMutex);
		old = scene;
		scene = ref;
	}

	if (old) {
		obs_source_dec_active(old);
		obs_source_dec_showing(old);
		obs_source_release(old);
	}
}

obs_source_t *VerticalCanvas::GetScene()
{
	std::lock_guard<std::mutex> lock(sceneMutex);
	return scene ? obs_source_get_ref(scene) : nullptr;
}

gs_texture_t *VerticalCanvas::GetTexture() const
{
	return texrender ? gs_texrender_get_texture(texrender) : nullptr;
}

/* ------------------------------------------------------------------ */
/* Render (thread grafica)                                             */
/* ------------------------------------------------------------------ */

void VerticalCanvas::RenderCallback(void *param, uint32_t, uint32_t)
{
	static_cast<VerticalCanvas *>(param)->Render();
}

void VerticalCanvas::Render()
{
	if (!video || !texrender || !stagesurf)
		return;

	obs_source_t *s = nullptr;
	{
		std::lock_guard<std::mutex> lock(sceneMutex);
		if (scene)
			s = obs_source_get_ref(scene);
	}

	gs_texrender_reset(texrender);
	if (gs_texrender_begin(texrender, width, height)) {
		vec4 clear;
		vec4_set(&clear, 0.0f, 0.0f, 0.0f, 1.0f);
		gs_clear(GS_CLEAR_COLOR, &clear, 0.0f, 0);
		gs_ortho(0.0f, (float)width, 0.0f, (float)height, -100.0f,
			 100.0f);

		if (s)
			obs_source_video_render(s);

		gs_texrender_end(texrender);

		if (activeOutputs.load() > 0) {
			gs_texture_t *tex = gs_texrender_get_texture(texrender);
			if (tex)
				CopyToVideo(tex);
		}
	}

	if (s)
		obs_source_release(s);
}

void VerticalCanvas::CopyToVideo(gs_texture_t *tex)
{
	gs_stage_texture(stagesurf, tex);

	uint8_t *data = nullptr;
	uint32_t linesize = 0;
	if (!gs_stagesurface_map(stagesurf, &data, &linesize))
		return;

	struct video_frame frame = {};
	if (video_output_lock_frame(video, &frame, 1, os_gettime_ns())) {
		const uint32_t sourceRowBytes = (linesize < frame.linesize[0])
						? linesize
						: frame.linesize[0];
		const uint32_t maxRowBytes = width * 4;
		const uint32_t rowBytes = (maxRowBytes < sourceRowBytes)
					? maxRowBytes
					: sourceRowBytes;
		for (uint32_t y = 0; y < height; y++) {
			memcpy(frame.data[0] + (size_t)y * frame.linesize[0],
			       data + (size_t)y * linesize, rowBytes);
		}
		video_output_unlock_frame(video);
	}

	gs_stagesurface_unmap(stagesurf);
}

/* ------------------------------------------------------------------ */
/* Encoders e saidas                                                   */
/* ------------------------------------------------------------------ */

bool VerticalCanvas::MakeEncoders(Out *o, int videoKbps, int audioKbps,
				  std::string &error)
{
	obs_data_t *vs = obs_data_create();
	obs_data_set_string(vs, "rate_control", "CBR");
	obs_data_set_int(vs, "bitrate", videoKbps);
	obs_data_set_string(vs, "preset", "veryfast");
	obs_data_set_int(vs, "keyint_sec", 2);
	o->venc = obs_video_encoder_create("obs_x264", "vertical_canvas_video",
					   vs, nullptr);
	obs_data_release(vs);

	obs_data_t *as = obs_data_create();
	obs_data_set_int(as, "bitrate", audioKbps);
	o->aenc = obs_audio_encoder_create("ffmpeg_aac", "vertical_canvas_audio",
					   as, 0, nullptr);
	obs_data_release(as);

	if (!o->venc || !o->aenc) {
		error = "Não foi possível criar os encoders (x264 / AAC).";
		return false;
	}

	obs_encoder_set_video(o->venc, video);
	obs_encoder_set_audio(o->aenc, obs_get_audio());
	return true;
}

void VerticalCanvas::DestroyOut(Out *o)
{
	if (!o)
		return;
	if (o->output) {
		signal_handler_disconnect(
			obs_output_get_signal_handler(o->output), "stop",
			StopSignal, o);
		if (obs_output_active(o->output))
			obs_output_force_stop(o->output);
		obs_output_release(o->output);
	}
	if (o->venc)
		obs_encoder_release(o->venc);
	if (o->aenc)
		obs_encoder_release(o->aenc);
	if (o->service)
		obs_service_release(o->service);
	delete o;
}

void VerticalCanvas::ReleaseOut(Out *&o)
{
	if (!o)
		return;
	DestroyOut(o);
	o = nullptr;
	activeOutputs--;
}

void VerticalCanvas::StopSignal(void *data, calldata_t *cd)
{
	Out *o = static_cast<Out *>(data);
	const int code = (int)calldata_int(cd, "code");
	if (code != OBS_OUTPUT_SUCCESS) {
		const char *last = o->output ? obs_output_get_last_error(o->output)
					     : nullptr;
		o->error = (last && *last)
				   ? last
				   : ("codigo de erro " + std::to_string(code));
	}
	o->state = 3;
}

bool VerticalCanvas::StartRecording(const std::string &path, int videoKbps,
				    int audioKbps, std::string &error)
{
	if (!initialized || !video) {
		error = "Canvas não inicializado.";
		return false;
	}
	if (rec) {
		error = "Já está gravando.";
		return false;
	}

	Out *o = new Out;
	if (!MakeEncoders(o, videoKbps, audioKbps, error)) {
		DestroyOut(o);
		return false;
	}

	obs_data_t *s = obs_data_create();
	obs_data_set_string(s, "path", path.c_str());
	o->output = obs_output_create("ffmpeg_muxer", "vertical_canvas_record",
				      s, nullptr);
	obs_data_release(s);
	if (!o->output) {
		error = "Não foi possível criar a saída de gravação.";
		DestroyOut(o);
		return false;
	}

	obs_output_set_video_encoder(o->output, o->venc);
	obs_output_set_audio_encoder(o->output, o->aenc, 0);
	signal_handler_connect(obs_output_get_signal_handler(o->output), "stop",
			       StopSignal, o);

	rec = o;
	activeOutputs++;

	if (!obs_output_start(o->output)) {
		const char *last = obs_output_get_last_error(o->output);
		error = (last && *last) ? last : "Falha ao iniciar a gravação.";
		ReleaseOut(rec);
		return false;
	}
	return true;
}

bool VerticalCanvas::StartStreaming(const std::string &server,
				    const std::string &key, int videoKbps,
				    int audioKbps, std::string &error)
{
	if (!initialized || !video) {
		error = "Canvas não inicializado.";
		return false;
	}
	if (stream) {
		error = "Já está ao vivo.";
		return false;
	}

	Out *o = new Out;
	if (!MakeEncoders(o, videoKbps, audioKbps, error)) {
		DestroyOut(o);
		return false;
	}

	obs_data_t *ss = obs_data_create();
	obs_data_set_string(ss, "server", server.c_str());
	obs_data_set_string(ss, "key", key.c_str());
	o->service = obs_service_create("rtmp_custom", "vertical_canvas_service",
					ss, nullptr);
	obs_data_release(ss);

	o->output = obs_output_create("rtmp_output", "vertical_canvas_stream",
				      nullptr, nullptr);
	if (!o->service || !o->output) {
		error = "Não foi possível criar a saída de live (RTMP).";
		DestroyOut(o);
		return false;
	}

	obs_output_set_video_encoder(o->output, o->venc);
	obs_output_set_audio_encoder(o->output, o->aenc, 0);
	obs_output_set_service(o->output, o->service);
	obs_output_set_reconnect_settings(o->output, 20, 10);
	signal_handler_connect(obs_output_get_signal_handler(o->output), "stop",
			       StopSignal, o);

	stream = o;
	activeOutputs++;

	if (!obs_output_start(o->output)) {
		const char *last = obs_output_get_last_error(o->output);
		error = (last && *last) ? last : "Falha ao iniciar a live.";
		ReleaseOut(stream);
		return false;
	}
	return true;
}

void VerticalCanvas::StopRecording()
{
	if (rec && rec->output && rec->state.load() == 1)
		obs_output_stop(rec->output);
}

void VerticalCanvas::StopStreaming()
{
	if (stream && stream->output && stream->state.load() == 1)
		obs_output_stop(stream->output);
}

std::string VerticalCanvas::Poll()
{
	std::string msg;

	if (rec && rec->state.load() == 3) {
		const std::string err = rec->error;
		ReleaseOut(rec);
		if (!err.empty())
			msg += "Gravação: " + err + " ";
	}
	if (stream && stream->state.load() == 3) {
		const std::string err = stream->error;
		ReleaseOut(stream);
		if (!err.empty())
			msg += "Live: " + err;
	}
	return msg;
}

void VerticalCanvas::StopAllAndWait()
{
	StopRecording();
	StopStreaming();

	for (int i = 0; i < 200; i++) {
		const bool recDone = !rec || rec->state.load() == 3;
		const bool streamDone = !stream || stream->state.load() == 3;
		if (recDone && streamDone)
			break;
		os_sleep_ms(50);
	}

	ReleaseOut(rec);
	ReleaseOut(stream);
}
