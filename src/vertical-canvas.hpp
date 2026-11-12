#pragma once

#include <obs.h>
#include <graphics/graphics.h>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>

/*
 * Motor do canvas vertical para OBS 27.x.
 *
 * Como o OBS 27 nao tem obs_view_add2() (resolucao propria por view),
 * renderizamos a cena escolhida nos mesmos "main render callbacks" do OBS
 * em uma textura propria (ex.: 1080x1920), copiamos para um video_t proprio
 * e ligamos encoders/saidas (gravacao e live) nesse video_t.
 */
class VerticalCanvas {
public:
	VerticalCanvas() = default;
	~VerticalCanvas();

	bool EnsureInit(uint32_t width, uint32_t height);
	bool Reconfigure(uint32_t width, uint32_t height);
	void Shutdown();

	// Escolhe a cena (source do tipo scene). Passe nullptr para limpar.
	void SetScene(obs_source_t *scene);
	// Devolve uma referencia (chame obs_source_release depois).
	obs_source_t *GetScene();
	// So use na thread grafica (callbacks de display).
	gs_texture_t *GetTexture() const;

	uint32_t Width() const { return width; }
	uint32_t Height() const { return height; }
	bool Initialized() const { return initialized; }

	bool StartRecording(const std::string &path, int videoKbps,
			    int audioKbps, std::string &error);
	bool StartStreaming(const std::string &server, const std::string &key,
			    int videoKbps, int audioKbps, std::string &error);
	void StopRecording();
	void StopStreaming();

	bool IsRecording() const { return rec != nullptr; }
	bool IsStreaming() const { return stream != nullptr; }
	bool Busy() const { return rec != nullptr || stream != nullptr; }

	// Chame periodicamente (thread da UI). Libera saidas que terminaram
	// e devolve texto de erro, se houve.
	std::string Poll();
	// Para tudo e espera (usado ao fechar o OBS).
	void StopAllAndWait();

private:
	struct Out {
		obs_output_t *output = nullptr;
		obs_encoder_t *venc = nullptr;
		obs_encoder_t *aenc = nullptr;
		obs_service_t *service = nullptr;
		std::atomic<int> state{1}; // 1 = em uso, 3 = terminou
		std::string error;
	};

	bool CreateResources(uint32_t w, uint32_t h);
	void DestroyResources();

	static void RenderCallback(void *param, uint32_t cx, uint32_t cy);
	void Render();
	void CopyToVideo(gs_texture_t *tex);

	bool MakeEncoders(Out *o, int videoKbps, int audioKbps,
			  std::string &error);
	void DestroyOut(Out *o);
	void ReleaseOut(Out *&o);
	static void StopSignal(void *data, calldata_t *cd);

	bool initialized = false;
	uint32_t width = 0;
	uint32_t height = 0;

	video_t *video = nullptr;
	gs_texrender_t *texrender = nullptr;
	gs_stagesurf_t *stagesurf = nullptr;

	std::mutex sceneMutex;
	obs_source_t *scene = nullptr;

	std::atomic<int> activeOutputs{0};
	Out *rec = nullptr;
	Out *stream = nullptr;
};
