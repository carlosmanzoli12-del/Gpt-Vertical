#include <obs-module.h>
#include <obs-frontend-api.h>

#include <QAction>
#include <QDockWidget>
#include <QMainWindow>
#include <QObject>

#include "vertical-dock.hpp"

OBS_DECLARE_MODULE()
OBS_MODULE_AUTHOR("vertical-canvas-lite")

MODULE_EXPORT const char *obs_module_description(void)
{
	return "Canvas vertical usando cenas e fontes existentes do OBS (OBS 27.x)";
}

MODULE_EXPORT const char *obs_module_name(void)
{
	return "Vertical Canvas Lite";
}

static VerticalDock *g_dock = nullptr;

static void SaveCallback(obs_data_t *save_data, bool saving, void *)
{
	if (!g_dock)
		return;
	if (saving)
		g_dock->SaveData(save_data);
	else
		g_dock->LoadData(save_data);
}

static void EventCallback(enum obs_frontend_event event, void *)
{
	if (!g_dock)
		return;
	switch (event) {
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGING:
		g_dock->OnCollectionChanging();
		break;
	case OBS_FRONTEND_EVENT_SCENE_CHANGED:
	case OBS_FRONTEND_EVENT_SCENE_LIST_CHANGED:
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED:
		g_dock->OnActiveSceneChanged();
		break;
	case OBS_FRONTEND_EVENT_EXIT:
		g_dock->Shutdown();
		break;
	default:
		break;
	}
}

bool obs_module_load(void)
{
	auto *mainWindow =
		static_cast<QMainWindow *>(obs_frontend_get_main_window());
	if (!mainWindow)
		return false;

	auto *dock = new QDockWidget(QStringLiteral("Vertical Canvas Lite"),
				     mainWindow);
	dock->setObjectName(QStringLiteral("VerticalCanvasLiteDock"));
	dock->setAllowedAreas(Qt::AllDockWidgetAreas);

	g_dock = new VerticalDock(dock);
	dock->setWidget(g_dock);
	mainWindow->addDockWidget(Qt::RightDockWidgetArea, dock);

	obs_frontend_add_save_callback(SaveCallback, nullptr);
	obs_frontend_add_event_callback(EventCallback, nullptr);

	auto *action = static_cast<QAction *>(
		obs_frontend_add_tools_menu_qaction("Vertical Canvas Lite"));
	QObject::connect(action, &QAction::triggered, [dock]() {
		dock->show();
		dock->raise();
	});

	blog(LOG_INFO, "[vertical-canvas-lite] carregado");
	return true;
}

void obs_module_unload(void)
{
	obs_frontend_remove_event_callback(EventCallback, nullptr);
	obs_frontend_remove_save_callback(SaveCallback, nullptr);
	g_dock = nullptr;
}
