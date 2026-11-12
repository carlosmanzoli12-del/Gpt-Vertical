#pragma once

#include <QWidget>

#include <mutex>

#include <obs.h>

#include "vertical-canvas.hpp"

class QMouseEvent;
class QWheelEvent;
class QShowEvent;
class QResizeEvent;
class QPaintEvent;

/*
 * Preview do canvas vertical (obs_display).
 * - Clique: seleciona o item sob o mouse
 * - Arrastar: move o item selecionado
 * - Roda do mouse: aumenta/diminui o item selecionado
 */
class PreviewWidget : public QWidget {
	Q_OBJECT

public:
	explicit PreviewWidget(VerticalCanvas *canvas, QWidget *parent = nullptr);
	~PreviewWidget() override;

	void SetSelectedItem(obs_sceneitem_t *item);
	void DestroyDisplay();

	QPaintEngine *paintEngine() const override { return nullptr; }

signals:
	// O ponteiro so e valido durante o slot.
	void ItemPicked(obs_sceneitem_t *item);

protected:
	void showEvent(QShowEvent *event) override;
	void resizeEvent(QResizeEvent *event) override;
	void paintEvent(QPaintEvent *) override {}
	void mousePressEvent(QMouseEvent *event) override;
	void mouseMoveEvent(QMouseEvent *event) override;
	void mouseReleaseEvent(QMouseEvent *event) override;
	void wheelEvent(QWheelEvent *event) override;

private:
	void CreateDisplay();
	void Layout(float &scale, float &ox, float &oy) const;
	void ToCanvas(const QPoint &p, float &x, float &y) const;
	obs_sceneitem_t *GetSelectedRef();

	static void DrawCallback(void *data, uint32_t cx, uint32_t cy);
	void Draw(uint32_t cx, uint32_t cy);

	VerticalCanvas *canvas;
	obs_display_t *display = nullptr;

	std::mutex selMutex;
	obs_sceneitem_t *selected = nullptr;

	bool dragging = false;
	float lastX = 0.0f;
	float lastY = 0.0f;
};
