#include "preview-widget.hpp"

#include <util/threading.h>

#include <obs-scene.h>

#include <QMouseEvent>
#include <QWheelEvent>

#include <graphics/graphics.h>
#include <graphics/matrix4.h>
#include <graphics/vec2.h>
#include <graphics/vec3.h>
#include <graphics/vec4.h>

#include <algorithm>
#include <cmath>

#ifdef _WIN32
#include <windows.h>
#endif

static void ComputeLayout(float cx, float cy, float cw, float ch, float &scale,
			  float &ox, float &oy)
{
	if (cw <= 0.0f || ch <= 0.0f || cx <= 0.0f || cy <= 0.0f) {
		scale = 1.0f;
		ox = oy = 0.0f;
		return;
	}
	scale = std::min(cx / cw, cy / ch);
	ox = (cx - cw * scale) * 0.5f;
	oy = (cy - ch * scale) * 0.5f;
}

PreviewWidget::PreviewWidget(VerticalCanvas *c, QWidget *parent)
	: QWidget(parent), canvas(c)
{
	setAttribute(Qt::WA_PaintOnScreen);
	setAttribute(Qt::WA_StaticContents);
	setAttribute(Qt::WA_NoSystemBackground);
	setAttribute(Qt::WA_OpaquePaintEvent);
	setAttribute(Qt::WA_DontCreateNativeAncestors);
	setAttribute(Qt::WA_NativeWindow);
}

PreviewWidget::~PreviewWidget()
{
	DestroyDisplay();
}

/* ------------------------------------------------------------------ */
/* Display                                                             */
/* ------------------------------------------------------------------ */

void PreviewWidget::CreateDisplay()
{
	if (display || !isVisible())
		return;

	const qreal dpr = devicePixelRatioF();

	gs_init_data info = {};
	info.cx = (uint32_t)std::max(1, (int)(width() * dpr));
	info.cy = (uint32_t)std::max(1, (int)(height() * dpr));
	info.format = GS_BGRA;
	info.zsformat = GS_ZS_NONE;
#ifdef _WIN32
	info.window.hwnd = (HWND)winId();
#endif

	// Se o compilador reclamar do numero de argumentos, remova o 2o
	// argumento (cor de fundo): ele nao existe em algumas versoes.
	display = obs_display_create(&info, 0xFF1E1E1E);
	if (display)
		obs_display_add_draw_callback(display, DrawCallback, this);
}

void PreviewWidget::DestroyDisplay()
{
	if (display) {
		obs_display_destroy(display);
		display = nullptr;
	}
	SetSelectedItem(nullptr);
}

void PreviewWidget::showEvent(QShowEvent *event)
{
	QWidget::showEvent(event);
	CreateDisplay();
}

void PreviewWidget::resizeEvent(QResizeEvent *event)
{
	QWidget::resizeEvent(event);
	if (!display) {
		CreateDisplay();
		return;
	}
	const qreal dpr = devicePixelRatioF();
	obs_display_resize(display, (uint32_t)std::max(1, (int)(width() * dpr)),
			   (uint32_t)std::max(1, (int)(height() * dpr)));
}

/* ------------------------------------------------------------------ */
/* Selecao                                                             */
/* ------------------------------------------------------------------ */

void PreviewWidget::SetSelectedItem(obs_sceneitem_t *item)
{
	if (item)
		obs_sceneitem_addref(item);

	obs_sceneitem_t *old = nullptr;
	{
		std::lock_guard<std::mutex> lock(selMutex);
		old = selected;
		selected = item;
	}
	// Libera fora do lock (evita deadlock com a thread grafica).
	if (old)
		obs_sceneitem_release(old);
}

obs_sceneitem_t *PreviewWidget::GetSelectedRef()
{
	std::lock_guard<std::mutex> lock(selMutex);
	if (selected)
		obs_sceneitem_addref(selected);
	return selected;
}

/* ------------------------------------------------------------------ */
/* Desenho (thread grafica)                                            */
/* ------------------------------------------------------------------ */

void PreviewWidget::DrawCallback(void *data, uint32_t cx, uint32_t cy)
{
	static_cast<PreviewWidget *>(data)->Draw(cx, cy);
}

void PreviewWidget::Draw(uint32_t cx, uint32_t cy)
{
	gs_texture_t *tex = canvas->GetTexture();
	if (!tex)
		return;

	const float cw = (float)canvas->Width();
	const float ch = (float)canvas->Height();

	float scale, ox, oy;
	ComputeLayout((float)cx, (float)cy, cw, ch, scale, ox, oy);

	gs_viewport_push();
	gs_projection_push();
	gs_ortho(0.0f, (float)cx, 0.0f, (float)cy, -100.0f, 100.0f);

	gs_matrix_push();
	gs_matrix_translate3f(ox, oy, 0.0f);
	gs_matrix_scale3f(scale, scale, 1.0f);

	gs_effect_t *effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
	gs_eparam_t *image = gs_effect_get_param_by_name(effect, "image");
	gs_effect_set_texture(image, tex);
	while (gs_effect_loop(effect, "Draw"))
		gs_draw_sprite(tex, 0, (uint32_t)cw, (uint32_t)ch);

	// Contorno do item selecionado
	{
		std::lock_guard<std::mutex> lock(selMutex);
		if (selected) {
			matrix4 box;
			obs_sceneitem_get_box_transform(selected, &box);

			gs_effect_t *solid = obs_get_base_effect(OBS_EFFECT_SOLID);
			gs_eparam_t *color =
				gs_effect_get_param_by_name(solid, "color");
			vec4 c;
			vec4_set(&c, 1.0f, 0.25f, 0.25f, 1.0f);
			gs_effect_set_vec4(color, &c);

			gs_matrix_push();
			gs_matrix_mul(&box);

			gs_render_start(true);
			gs_vertex2f(0.0f, 0.0f);
			gs_vertex2f(1.0f, 0.0f);
			gs_vertex2f(1.0f, 1.0f);
			gs_vertex2f(0.0f, 1.0f);
			gs_vertex2f(0.0f, 0.0f);
			gs_vertbuffer_t *vb = gs_render_save();

			gs_load_vertexbuffer(vb);
			while (gs_effect_loop(solid, "Solid"))
				gs_draw(GS_LINESTRIP, 0, 0);
			gs_vertexbuffer_destroy(vb);

			gs_matrix_pop();
		}
	}

	gs_matrix_pop();
	gs_projection_pop();
	gs_viewport_pop();
}

/* ------------------------------------------------------------------ */
/* Mouse                                                               */
/* ------------------------------------------------------------------ */

void PreviewWidget::Layout(float &scale, float &ox, float &oy) const
{
	const float dpr = (float)devicePixelRatioF();
	ComputeLayout(width() * dpr, height() * dpr, (float)canvas->Width(),
		      (float)canvas->Height(), scale, ox, oy);
}

void PreviewWidget::ToCanvas(const QPoint &p, float &x, float &y) const
{
	float scale, ox, oy;
	Layout(scale, ox, oy);
	const float dpr = (float)devicePixelRatioF();
	x = (p.x() * dpr - ox) / scale;
	y = (p.y() * dpr - oy) / scale;
}

namespace {
struct PickData {
	float x;
	float y;
	obs_sceneitem_t *item;
};

bool PickCallback(obs_scene_t *, obs_sceneitem_t *item, void *param)
{
	auto *pd = static_cast<PickData *>(param);
	if (!obs_sceneitem_visible(item) || obs_sceneitem_locked(item))
		return true;

	matrix4 box, inv;
	obs_sceneitem_get_box_transform(item, &box);
	matrix4_inv(&inv, &box);

	vec3 p;
	vec3_set(&p, pd->x, pd->y, 0.0f);
	vec3_transform(&p, &p, &inv);

	// O ultimo item acertado e o que esta mais por cima.
	if (p.x >= 0.0f && p.x <= 1.0f && p.y >= 0.0f && p.y <= 1.0f) {
		obs_sceneitem_addref(item);
		if (pd->item)
			obs_sceneitem_release(pd->item);
		pd->item = item;
	}
	return true;
}
} // namespace

void PreviewWidget::mousePressEvent(QMouseEvent *event)
{
	if (event->button() != Qt::LeftButton)
		return;

	float x, y;
	ToCanvas(event->pos(), x, y);

	obs_source_t *sceneSrc = canvas->GetScene();
	if (!sceneSrc)
		return;

	PickData pd{x, y, nullptr};
	if (obs_scene_t *scene = obs_scene_from_source(sceneSrc))
		obs_scene_enum_items(scene, PickCallback, &pd);

	if (pd.item) {
		emit ItemPicked(pd.item);
		obs_sceneitem_release(pd.item);
		dragging = true;
		lastX = x;
		lastY = y;
	}
	obs_source_release(sceneSrc);
}

void PreviewWidget::mouseMoveEvent(QMouseEvent *event)
{
	if (!dragging)
		return;

	float x, y;
	ToCanvas(event->pos(), x, y);

	if (obs_sceneitem_t *item = GetSelectedRef()) {
		vec2 pos;
		obs_sceneitem_get_pos(item, &pos);
		pos.x += x - lastX;
		pos.y += y - lastY;
		obs_sceneitem_set_pos(item, &pos);
		obs_sceneitem_release(item);
	}
	lastX = x;
	lastY = y;
}

void PreviewWidget::mouseReleaseEvent(QMouseEvent *event)
{
	if (event->button() == Qt::LeftButton)
		dragging = false;
}

void PreviewWidget::wheelEvent(QWheelEvent *event)
{
	obs_sceneitem_t *item = GetSelectedRef();
	if (!item)
		return;

	const float factor = std::pow(1.1f, event->angleDelta().y() / 120.0f);

	if (obs_sceneitem_get_bounds_type(item) != OBS_BOUNDS_NONE) {
		vec2 b;
		obs_sceneitem_get_bounds(item, &b);
		vec2_mulf(&b, &b, factor);
		obs_sceneitem_set_bounds(item, &b);
	} else {
		vec2 s;
		obs_sceneitem_get_scale(item, &s);
		vec2_mulf(&s, &s, factor);
		obs_sceneitem_set_scale(item, &s);
	}
	obs_sceneitem_release(item);
	event->accept();
}
