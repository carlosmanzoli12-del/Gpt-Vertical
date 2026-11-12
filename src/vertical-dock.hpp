#pragma once

#include <QString>
#include <QTimer>
#include <QWidget>

#include <string>
#include <vector>

#include <obs.h>

#include "vertical-canvas.hpp"

class QListWidget;
class QComboBox;
class QListWidgetItem;
class QPushButton;
class QLabel;
class PreviewWidget;

struct VCSettings {
	uint32_t width = 1080;
	uint32_t height = 1920;
	int videoKbps = 6000;
	int audioKbps = 160;
	std::string recordDir;
	std::string server;
	std::string key;
};

class VerticalDock : public QWidget {
	Q_OBJECT

public:
	explicit VerticalDock(QWidget *parent = nullptr);
	~VerticalDock() override;

	void Shutdown();
	void SaveData(obs_data_t *root);
	void LoadData(obs_data_t *root);
	void OnCollectionChanging();
	void OnActiveSceneChanged();

public slots:
	void RefreshSources();

private slots:
	void SceneRowChanged(int row);
	void SceneModeChanged(int mode);

	void AddSource();
	void RemoveSource();
	void MoveSourceUp();
	void MoveSourceDown();
	void OpenProperties();
	void SourceRowChanged(int row);
	void SourceItemChanged(QListWidgetItem *item);
	void OnItemPicked(obs_sceneitem_t *item);

	void ToggleRecord();
	void ToggleStream();
	void OpenSettings();
	void PollOutputs();

private:
	struct SceneEntry {
		std::string name; // nome exato da cena existente no OBS
		obs_source_t *ref = nullptr;
	};

	void LoadSettings();
	void SaveSettings();

	void ClearScenes();
	void RefreshSceneChoices();
	void ConnectSceneSignals(obs_source_t *scene);
	void DisconnectSceneSignals();
	static void SceneSignalCallback(void *data, calldata_t *cd);

	obs_source_t *CurrentSceneSource() const;
	obs_sceneitem_t *CurrentItem() const;
	void SelectItemById(qlonglong id);
	void FitCurrent(int mode);
	void UpdateButtons();

	QString UniqueSourceName(const QString &base) const;

	VerticalCanvas canvas;
	VCSettings settings;

	PreviewWidget *preview = nullptr;
	QListWidget *sceneList = nullptr;
	QComboBox *sceneMode = nullptr;
	QString manualSceneName;
	QListWidget *sourceList = nullptr;
	QPushButton *recBtn = nullptr;
	QPushButton *streamBtn = nullptr;
	QLabel *statusLabel = nullptr;
	QTimer poll;

	std::vector<SceneEntry> scenes;
	obs_source_t *connectedScene = nullptr;
	QString lastMessage;
	bool shutdownDone = false;
};
