#include "vertical-dock.hpp"
#include "preview-widget.hpp"

#include <obs-frontend-api.h>
#include <util/threading.h>
#include <obs-scene.h>

#include <QAction>
#include <QCursor>
#include <QDateTime>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStringList>
#include <QVBoxLayout>

#include <algorithm>
#include <cstring>
#include <utility>

static QString U8(const char *s)
{
	return QString::fromUtf8(s);
}

static QPushButton *MakeButton(const QString &text, const QString &tip,
			       QWidget *parent)
{
	auto *b = new QPushButton(text, parent);
	b->setToolTip(tip);
	b->setMinimumWidth(30);
	return b;
}

/* ------------------------------------------------------------------ */
/* Construcao                                                          */
/* ------------------------------------------------------------------ */

VerticalDock::VerticalDock(QWidget *parent) : QWidget(parent)
{
	LoadSettings();
	canvas.EnsureInit(settings.width, settings.height);

	auto *root = new QVBoxLayout(this);
	root->setContentsMargins(4, 4, 4, 4);

	preview = new PreviewWidget(&canvas, this);
	preview->setMinimumSize(180, 320);
	preview->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	root->addWidget(preview, 4);

	auto *lists = new QHBoxLayout;

	// ---- Cenas existentes do OBS ----
	auto *sceneCol = new QVBoxLayout;
	sceneCol->addWidget(new QLabel(U8("Modo de seleção"), this));
	sceneMode = new QComboBox(this);
	sceneMode->addItem(U8("Automático — cena ativa do OBS"), 0);
	sceneMode->addItem(U8("Manual — escolher cena"), 1);
	sceneCol->addWidget(sceneMode);
	sceneCol->addWidget(new QLabel(U8("Cenas do OBS"), this));
	sceneList = new QListWidget(this);
	sceneList->setEnabled(false); // começa no modo automático
	sceneCol->addWidget(sceneList, 1);
	auto *sceneNotice = new QLabel(
		U8("Ajustes de fontes alteram a cena original do OBS."), this);
	sceneNotice->setWordWrap(true);
	sceneCol->addWidget(sceneNotice);
	lists->addLayout(sceneCol, 1);

	// ---- Fontes ----
	auto *srcCol = new QVBoxLayout;
	srcCol->addWidget(new QLabel(U8("Fontes"), this));
	sourceList = new QListWidget(this);
	srcCol->addWidget(sourceList, 1);
	auto *srcBtns = new QHBoxLayout;
	auto *addSrc = MakeButton("+", U8("Adicionar fonte"), this);
	auto *delSrc = MakeButton(U8("−"), U8("Remover fonte"), this);
	auto *upSrc = MakeButton(U8("▲"), U8("Subir"), this);
	auto *downSrc = MakeButton(U8("▼"), U8("Descer"), this);
	auto *propSrc = MakeButton(U8("⚙"), U8("Propriedades"), this);
	auto *fitBtn = MakeButton(U8("Ajustar"), U8("Ajustar à tela"), this);
	auto *fitMenu = new QMenu(fitBtn);
	connect(fitMenu->addAction(U8("Ajustar à tela (conter)")),
		&QAction::triggered, this, [this] { FitCurrent(0); });
	connect(fitMenu->addAction(U8("Preencher a tela (cobrir)")),
		&QAction::triggered, this, [this] { FitCurrent(1); });
	connect(fitMenu->addAction(U8("Esticar na tela")), &QAction::triggered,
		this, [this] { FitCurrent(2); });
	connect(fitMenu->addAction(U8("Resetar transformação")),
		&QAction::triggered, this, [this] { FitCurrent(3); });
	fitBtn->setMenu(fitMenu);
	srcBtns->addWidget(addSrc);
	srcBtns->addWidget(delSrc);
	srcBtns->addWidget(upSrc);
	srcBtns->addWidget(downSrc);
	srcBtns->addWidget(propSrc);
	srcBtns->addWidget(fitBtn);
	srcBtns->addStretch(1);
	srcCol->addLayout(srcBtns);
	lists->addLayout(srcCol, 1);

	root->addLayout(lists, 2);

	// ---- Controles ----
	auto *bottom = new QHBoxLayout;
	recBtn = new QPushButton(this);
	streamBtn = new QPushButton(this);
	auto *settingsBtn = new QPushButton(U8("Configurações"), this);
	bottom->addWidget(recBtn);
	bottom->addWidget(streamBtn);
	bottom->addWidget(settingsBtn);
	root->addLayout(bottom);

	statusLabel = new QLabel(this);
	statusLabel->setWordWrap(true);
	root->addWidget(statusLabel);

	connect(sceneList, &QListWidget::currentRowChanged, this,
		&VerticalDock::SceneRowChanged);
	connect(sceneMode, QOverload<int>::of(&QComboBox::currentIndexChanged),
		this, &VerticalDock::SceneModeChanged);

	connect(addSrc, &QPushButton::clicked, this, &VerticalDock::AddSource);
	connect(delSrc, &QPushButton::clicked, this,
		&VerticalDock::RemoveSource);
	connect(upSrc, &QPushButton::clicked, this, &VerticalDock::MoveSourceUp);
	connect(downSrc, &QPushButton::clicked, this,
		&VerticalDock::MoveSourceDown);
	connect(propSrc, &QPushButton::clicked, this,
		&VerticalDock::OpenProperties);
	connect(sourceList, &QListWidget::currentRowChanged, this,
		&VerticalDock::SourceRowChanged);
	connect(sourceList, &QListWidget::itemChanged, this,
		&VerticalDock::SourceItemChanged);
	connect(sourceList, &QListWidget::itemDoubleClicked, this,
		[this](QListWidgetItem *) { OpenProperties(); });
	connect(preview, &PreviewWidget::ItemPicked, this,
		&VerticalDock::OnItemPicked);

	connect(recBtn, &QPushButton::clicked, this, &VerticalDock::ToggleRecord);
	connect(streamBtn, &QPushButton::clicked, this,
		&VerticalDock::ToggleStream);
	connect(settingsBtn, &QPushButton::clicked, this,
		&VerticalDock::OpenSettings);

	poll.setInterval(500);
	connect(&poll, &QTimer::timeout, this, &VerticalDock::PollOutputs);
	poll.start();

	RefreshSceneChoices();
	UpdateButtons();
}

VerticalDock::~VerticalDock()
{
	Shutdown();
}

void VerticalDock::Shutdown()
{
	if (shutdownDone)
		return;
	shutdownDone = true;

	poll.stop();
	canvas.StopAllAndWait();
	DisconnectSceneSignals();
	canvas.SetScene(nullptr);
	preview->DestroyDisplay();
	canvas.Shutdown();

	// Mantem os nomes (para salvar) mas solta as referencias.
	for (auto &s : scenes) {
		if (s.ref) {
			obs_source_release(s.ref);
			s.ref = nullptr;
		}
	}
	setEnabled(false);
}

/* ------------------------------------------------------------------ */
/* Configuracoes (QSettings, por usuario)                              */
/* ------------------------------------------------------------------ */

void VerticalDock::LoadSettings()
{
	QSettings qs(QStringLiteral("VerticalCanvasLite"),
		     QStringLiteral("settings"));
	settings.width = qs.value("width", 1080).toUInt() & ~1u;
	settings.height = qs.value("height", 1920).toUInt() & ~1u;
	settings.videoKbps = qs.value("videoKbps", 6000).toInt();
	settings.audioKbps = qs.value("audioKbps", 160).toInt();
	settings.recordDir =
		qs.value("recordDir",
			 QStandardPaths::writableLocation(
				 QStandardPaths::MoviesLocation))
			.toString()
			.toStdString();
	settings.server = qs.value("server", "").toString().toStdString();
	settings.key = qs.value("key", "").toString().toStdString();

	if (settings.width < 2)
		settings.width = 1080;
	if (settings.height < 2)
		settings.height = 1920;
}

void VerticalDock::SaveSettings()
{
	QSettings qs(QStringLiteral("VerticalCanvasLite"),
		     QStringLiteral("settings"));
	qs.setValue("width", settings.width);
	qs.setValue("height", settings.height);
	qs.setValue("videoKbps", settings.videoKbps);
	qs.setValue("audioKbps", settings.audioKbps);
	qs.setValue("recordDir", QString::fromStdString(settings.recordDir));
	qs.setValue("server", QString::fromStdString(settings.server));
	qs.setValue("key", QString::fromStdString(settings.key));
}

void VerticalDock::OpenSettings()
{
	QDialog dlg(this);
	dlg.setWindowTitle(U8("Vertical Canvas – Configurações"));
	auto *form = new QFormLayout(&dlg);

	auto *w = new QSpinBox(&dlg);
	w->setRange(2, 7680);
	w->setSingleStep(2);
	w->setValue((int)settings.width);
	auto *h = new QSpinBox(&dlg);
	h->setRange(2, 7680);
	h->setSingleStep(2);
	h->setValue((int)settings.height);
	auto *vb = new QSpinBox(&dlg);
	vb->setRange(500, 50000);
	vb->setSuffix(" kbps");
	vb->setValue(settings.videoKbps);
	auto *ab = new QSpinBox(&dlg);
	ab->setRange(64, 320);
	ab->setSuffix(" kbps");
	ab->setValue(settings.audioKbps);

	auto *dirEdit = new QLineEdit(QString::fromStdString(settings.recordDir),
				      &dlg);
	auto *browse = new QPushButton("...", &dlg);
	auto *dirRow = new QHBoxLayout;
	dirRow->addWidget(dirEdit, 1);
	dirRow->addWidget(browse);
	connect(browse, &QPushButton::clicked, &dlg, [&] {
		const QString d = QFileDialog::getExistingDirectory(
			&dlg, U8("Pasta das gravações"), dirEdit->text());
		if (!d.isEmpty())
			dirEdit->setText(d);
	});

	auto *server =
		new QLineEdit(QString::fromStdString(settings.server), &dlg);
	server->setPlaceholderText("rtmp://...");
	auto *key = new QLineEdit(QString::fromStdString(settings.key), &dlg);
	key->setEchoMode(QLineEdit::Password);

	form->addRow(U8("Largura"), w);
	form->addRow(U8("Altura"), h);
	form->addRow(U8("Bitrate de vídeo"), vb);
	form->addRow(U8("Bitrate de áudio"), ab);
	form->addRow(U8("Pasta das gravações"), dirRow);
	form->addRow(U8("Servidor (live)"), server);
	form->addRow(U8("Chave da live"), key);

	auto *bb = new QDialogButtonBox(
		QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
	connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
	connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
	form->addRow(bb);

	if (dlg.exec() != QDialog::Accepted)
		return;

	const uint32_t nw = (uint32_t)w->value() & ~1u;
	const uint32_t nh = (uint32_t)h->value() & ~1u;
	if (nw != settings.width || nh != settings.height) {
		if (canvas.Busy()) {
			QMessageBox::information(
				this, U8("Vertical Canvas"),
				U8("Pare a gravação e a live para mudar a resolução."));
		} else if (canvas.Reconfigure(nw, nh)) {
			settings.width = nw;
			settings.height = nh;
		} else {
			QMessageBox::warning(
				this, U8("Vertical Canvas"),
				U8("Não foi possível aplicar a nova resolução."));
		}
	}

	settings.videoKbps = vb->value();
	settings.audioKbps = ab->value();
	settings.recordDir = dirEdit->text().toStdString();
	settings.server = server->text().trimmed().toStdString();
	settings.key = key->text().trimmed().toStdString();
	SaveSettings();
}

/* ------------------------------------------------------------------ */
/* Salvar / carregar (dentro da colecao de cenas do OBS)               */
/* ------------------------------------------------------------------ */

void VerticalDock::SaveData(obs_data_t *root)
{
	obs_data_t *data = obs_data_create();
	obs_data_set_int(data, "scene_mode", sceneMode ? sceneMode->currentIndex() : 0);
	const int row = sceneList ? sceneList->currentRow() : -1;
	if (row >= 0 && row < (int)scenes.size())
		obs_data_set_string(data, "manual_scene", scenes[row].name.c_str());
	obs_data_set_obj(root, "vertical_canvas_lite", data);
	obs_data_release(data);
}

void VerticalDock::LoadData(obs_data_t *root)
{
	if (shutdownDone)
		return;

	obs_data_t *data = obs_data_get_obj(root, "vertical_canvas_lite");
	if (data) {
		const int mode = (int)obs_data_get_int(data, "scene_mode");
		if (sceneMode)
			sceneMode->setCurrentIndex(mode == 1 ? 1 : 0);
		const char *name = obs_data_get_string(data, "manual_scene");
		manualSceneName = QString::fromUtf8(name ? name : "");
		obs_data_release(data);
	}
	RefreshSceneChoices();
}

void VerticalDock::OnCollectionChanging()
{
	if (shutdownDone)
		return;
	ClearScenes();
}

void VerticalDock::OnActiveSceneChanged()
{
	if (shutdownDone)
		return;
	// Em modo manual a cena escolhida permanece; em automático acompanha a ativa.
	// A mesma atualização também recarrega a lista quando cenas são adicionadas/removidas.
	RefreshSceneChoices();
}

void VerticalDock::SceneModeChanged(int mode)
{
	const bool manual = mode == 1;
	sceneList->setEnabled(manual);
	if (manual && manualSceneName.isEmpty()) {
		obs_source_t *active = obs_frontend_get_current_scene();
		if (active) {
			manualSceneName = QString::fromUtf8(obs_source_get_name(active));
			obs_source_release(active);
		}
	}
	RefreshSceneChoices();
}

void VerticalDock::RefreshSceneChoices()
{
	if (shutdownDone || !sceneList)
		return;

	QString wanted = manualSceneName;
	const bool manual = sceneMode && sceneMode->currentIndex() == 1;
	if (!manual) {
		obs_source_t *active = obs_frontend_get_current_scene();
		if (active) {
			wanted = QString::fromUtf8(obs_source_get_name(active));
			obs_source_release(active);
		}
	}

	DisconnectSceneSignals();
	canvas.SetScene(nullptr);
	preview->SetSelectedItem(nullptr);

	for (auto &entry : scenes) {
		if (entry.ref)
			obs_source_release(entry.ref);
	}
	scenes.clear();

	obs_frontend_source_list list = {};
	obs_frontend_get_scenes(&list);
	for (size_t i = 0; i < list.sources.num; ++i) {
		obs_source_t *source = list.sources.array[i];
		if (!source || !obs_scene_from_source(source))
			continue;
		SceneEntry entry;
		entry.name = obs_source_get_name(source);
		entry.ref = obs_source_get_ref(source);
		if (entry.ref)
			scenes.push_back(entry);
	}
	obs_frontend_source_list_free(&list);

	{
		QSignalBlocker blocker(sceneList);
		sceneList->clear();
		int selected = -1;
		for (int i = 0; i < (int)scenes.size(); ++i) {
			sceneList->addItem(QString::fromUtf8(scenes[i].name.c_str()));
			if (QString::fromUtf8(scenes[i].name.c_str()) == wanted)
				selected = i;
		}
		if (selected < 0 && !scenes.empty())
			selected = 0;
		if (selected >= 0)
			sceneList->setCurrentRow(selected);
	}

	if (manual && sceneList->currentRow() >= 0)
		manualSceneName = QString::fromUtf8(
			scenes[sceneList->currentRow()].name.c_str());

	SceneRowChanged(sceneList->currentRow());
}

void VerticalDock::ClearScenes()
{
	DisconnectSceneSignals();
	canvas.SetScene(nullptr);
	if (preview)
		preview->SetSelectedItem(nullptr);
	if (sceneList) {
		QSignalBlocker a(sceneList);
		QSignalBlocker b(sourceList);
		sceneList->clear();
		sourceList->clear();
	}
	for (auto &entry : scenes) {
		if (entry.ref)
			obs_source_release(entry.ref);
	}
	scenes.clear();
}

void VerticalDock::SceneRowChanged(int row)
{
	DisconnectSceneSignals();
	preview->SetSelectedItem(nullptr);

	obs_source_t *selected = nullptr;
	if (row >= 0 && row < (int)scenes.size()) {
		selected = obs_source_get_ref(scenes[row].ref);
		if (sceneMode && sceneMode->currentIndex() == 1)
			manualSceneName = QString::fromUtf8(scenes[row].name.c_str());
	}

	if (!selected) {
		canvas.SetScene(nullptr);
		QSignalBlocker b(sourceList);
		sourceList->clear();
		return;
	}

	canvas.EnsureInit(settings.width, settings.height);
	canvas.SetScene(selected);
	ConnectSceneSignals(selected);
	obs_source_release(selected);
	RefreshSources();
}

void VerticalDock::SceneSignalCallback(void *data, calldata_t *)
{
	QMetaObject::invokeMethod(static_cast<VerticalDock *>(data),
				  "RefreshSources", Qt::QueuedConnection);
}

void VerticalDock::ConnectSceneSignals(obs_source_t *scene)
{
	if (!scene)
		return;
	signal_handler_t *sh = obs_source_get_signal_handler(scene);
	static const char *sigs[] = {"item_add", "item_remove", "reorder",
				     "refresh"};
	for (const char *sig : sigs)
		signal_handler_connect(sh, sig, SceneSignalCallback, this);
	connectedScene = scene;
}

void VerticalDock::DisconnectSceneSignals()
{
	if (!connectedScene)
		return;
	signal_handler_t *sh = obs_source_get_signal_handler(connectedScene);
	static const char *sigs[] = {"item_add", "item_remove", "reorder",
				     "refresh"};
	for (const char *sig : sigs)
		signal_handler_disconnect(sh, sig, SceneSignalCallback, this);
	connectedScene = nullptr;
}

obs_source_t *VerticalDock::CurrentSceneSource() const
{
	const int row = sceneList->currentRow();
	if (row < 0 || row >= (int)scenes.size())
		return nullptr;
	return scenes[row].ref;
}

/* ------------------------------------------------------------------ */
/* Fontes                                                              */
/* ------------------------------------------------------------------ */

static bool EnumItemsCallback(obs_scene_t *, obs_sceneitem_t *item, void *param)
{
	auto *list = static_cast<QListWidget *>(param);
	obs_source_t *src = obs_sceneitem_get_source(item);

	auto *li = new QListWidgetItem(
		QString::fromUtf8(obs_source_get_name(src)));
	li->setFlags(li->flags() | Qt::ItemIsUserCheckable);
	li->setCheckState(obs_sceneitem_visible(item) ? Qt::Checked
						      : Qt::Unchecked);
	li->setData(Qt::UserRole, (qlonglong)obs_sceneitem_get_id(item));
	// A enumeracao vai de baixo para cima; a lista mostra o topo primeiro.
	list->insertItem(0, li);
	return true;
}

void VerticalDock::RefreshSources()
{
	if (shutdownDone)
		return;

	qlonglong keep = -1;
	if (auto *cur = sourceList->currentItem())
		keep = cur->data(Qt::UserRole).toLongLong();

	{
		QSignalBlocker b(sourceList);
		sourceList->clear();
		if (obs_source_t *src = CurrentSceneSource()) {
			if (obs_scene_t *scene = obs_scene_from_source(src))
				obs_scene_enum_items(scene, EnumItemsCallback,
						     sourceList);
		}
	}

	if (keep >= 0)
		SelectItemById(keep);
	SourceRowChanged(sourceList->currentRow());
}

obs_sceneitem_t *VerticalDock::CurrentItem() const
{
	auto *li = sourceList->currentItem();
	obs_source_t *src = CurrentSceneSource();
	if (!li || !src)
		return nullptr;
	obs_scene_t *scene = obs_scene_from_source(src);
	if (!scene)
		return nullptr;
	return obs_scene_find_sceneitem_by_id(
		scene, li->data(Qt::UserRole).toLongLong());
}

void VerticalDock::SelectItemById(qlonglong id)
{
	for (int i = 0; i < sourceList->count(); i++) {
		if (sourceList->item(i)->data(Qt::UserRole).toLongLong() == id) {
			QSignalBlocker b(sourceList);
			sourceList->setCurrentRow(i);
			return;
		}
	}
}

void VerticalDock::SourceRowChanged(int)
{
	preview->SetSelectedItem(CurrentItem());
}

void VerticalDock::OnItemPicked(obs_sceneitem_t *item)
{
	SelectItemById((qlonglong)obs_sceneitem_get_id(item));
	preview->SetSelectedItem(item);
}

void VerticalDock::SourceItemChanged(QListWidgetItem *li)
{
	obs_source_t *src = CurrentSceneSource();
	if (!li || !src)
		return;
	obs_scene_t *scene = obs_scene_from_source(src);
	if (!scene)
		return;
	obs_sceneitem_t *item = obs_scene_find_sceneitem_by_id(
		scene, li->data(Qt::UserRole).toLongLong());
	if (item)
		obs_sceneitem_set_visible(item, li->checkState() == Qt::Checked);
}

QString VerticalDock::UniqueSourceName(const QString &base) const
{
	QString name = base;
	for (int i = 2;; i++) {
		obs_source_t *s = obs_get_source_by_name(name.toUtf8().constData());
		if (!s)
			return name;
		obs_source_release(s);
		name = QString("%1 %2").arg(base).arg(i);
	}
}

static bool EnumExistingSources(void *param, obs_source_t *source)
{
	auto *names = static_cast<std::vector<QString> *>(param);
	names->push_back(QString::fromUtf8(obs_source_get_name(source)));
	return true;
}

void VerticalDock::AddSource()
{
	obs_source_t *sceneSrc = CurrentSceneSource();
	obs_scene_t *scene = sceneSrc ? obs_scene_from_source(sceneSrc) : nullptr;
	if (!scene) {
		QMessageBox::information(this, U8("Vertical Canvas"),
					 U8("Crie uma cena primeiro."));
		return;
	}

	QMenu menu(this);

	// ---- Nova fonte ----
	QMenu *newMenu = menu.addMenu(U8("Nova fonte"));
	std::vector<std::pair<QString, QString>> types;
	size_t idx = 0;
	const char *id = nullptr;
	while (obs_enum_input_types(idx++, &id)) {
		const uint32_t flags = obs_get_source_output_flags(id);
		if (flags & OBS_SOURCE_CAP_DISABLED)
			continue;
		if (flags & OBS_SOURCE_DEPRECATED)
			continue;
		types.emplace_back(U8(obs_source_get_display_name(id)), U8(id));
	}
	std::sort(types.begin(), types.end(), [](const auto &a, const auto &b) {
		return a.first.localeAwareCompare(b.first) < 0;
	});
	for (const auto &t : types)
		newMenu->addAction(t.first)->setData(QStringLiteral("new:") +
						     t.second);

	// ---- Fonte existente ----
	QMenu *existingMenu = menu.addMenu(U8("Fonte existente"));
	std::vector<QString> names;
	obs_enum_sources(EnumExistingSources, &names);
	std::sort(names.begin(), names.end(), [](const QString &a, const QString &b) {
		return a.localeAwareCompare(b) < 0;
	});
	for (const auto &n : names)
		existingMenu->addAction(n)->setData(QStringLiteral("ex:") + n);
	existingMenu->setEnabled(!names.empty());

	QAction *chosen = menu.exec(QCursor::pos());
	if (!chosen)
		return;

	const QString data = chosen->data().toString();
	obs_sceneitem_t *item = nullptr;
	obs_source_t *openProps = nullptr;

	if (data.startsWith("new:")) {
		const QByteArray typeId = data.mid(4).toUtf8();
		const QString base =
			U8(obs_source_get_display_name(typeId.constData()));

		bool ok = false;
		const QString name = QInputDialog::getText(
			this, U8("Nova fonte"), U8("Nome da fonte:"),
			QLineEdit::Normal, UniqueSourceName(base), &ok);
		if (!ok || name.trimmed().isEmpty())
			return;

		const QByteArray nameUtf8 = name.trimmed().toUtf8();
		if (obs_source_t *existing =
			    obs_get_source_by_name(nameUtf8.constData())) {
			obs_source_release(existing);
			QMessageBox::warning(
				this, U8("Vertical Canvas"),
				U8("Já existe uma fonte com esse nome."));
			return;
		}

		obs_source_t *src = obs_source_create(
			typeId.constData(), nameUtf8.constData(), nullptr,
			nullptr);
		if (!src) {
			QMessageBox::warning(
				this, U8("Vertical Canvas"),
				U8("Não foi possível criar a fonte."));
			return;
		}
		item = obs_scene_add(scene, src);
		obs_source_release(src); // a cena guarda a referência
		if (item)
			openProps = obs_sceneitem_get_source(item);
	} else if (data.startsWith("ex:")) {
		const QByteArray n = data.mid(3).toUtf8();
		obs_source_t *src = obs_get_source_by_name(n.constData());
		if (!src)
			return;
		item = obs_scene_add(scene, src);
		obs_source_release(src);
	}

	RefreshSources();
	if (item) {
		SelectItemById((qlonglong)obs_sceneitem_get_id(item));
		SourceRowChanged(sourceList->currentRow());
	}
	if (openProps)
		obs_frontend_open_source_properties(openProps);
}

void VerticalDock::RemoveSource()
{
	obs_sceneitem_t *item = CurrentItem();
	if (!item)
		return;
	preview->SetSelectedItem(nullptr);
	obs_sceneitem_remove(item);
}

void VerticalDock::MoveSourceUp()
{
	if (obs_sceneitem_t *item = CurrentItem())
		obs_sceneitem_set_order(item, OBS_ORDER_MOVE_UP);
}

void VerticalDock::MoveSourceDown()
{
	if (obs_sceneitem_t *item = CurrentItem())
		obs_sceneitem_set_order(item, OBS_ORDER_MOVE_DOWN);
}

void VerticalDock::OpenProperties()
{
	if (obs_sceneitem_t *item = CurrentItem())
		obs_frontend_open_source_properties(
			obs_sceneitem_get_source(item));
}

void VerticalDock::FitCurrent(int mode)
{
	obs_sceneitem_t *item = CurrentItem();
	if (!item)
		return;

	const float W = (float)canvas.Width();
	const float H = (float)canvas.Height();
	vec2 v;

	if (mode == 3) {
		obs_sceneitem_set_bounds_type(item, OBS_BOUNDS_NONE);
		vec2_set(&v, 1.0f, 1.0f);
		obs_sceneitem_set_scale(item, &v);
		obs_sceneitem_set_alignment(item, OBS_ALIGN_LEFT | OBS_ALIGN_TOP);
		vec2_zero(&v);
		obs_sceneitem_set_pos(item, &v);
		return;
	}

	const obs_bounds_type type = mode == 0   ? OBS_BOUNDS_SCALE_INNER
				     : mode == 1 ? OBS_BOUNDS_SCALE_OUTER
						 : OBS_BOUNDS_STRETCH;
	obs_sceneitem_set_bounds_type(item, type);
	obs_sceneitem_set_bounds_alignment(item, OBS_ALIGN_CENTER);
	vec2_set(&v, W, H);
	obs_sceneitem_set_bounds(item, &v);
	obs_sceneitem_set_alignment(item, OBS_ALIGN_CENTER);
	vec2_set(&v, W * 0.5f, H * 0.5f);
	obs_sceneitem_set_pos(item, &v);
}

/* ------------------------------------------------------------------ */
/* Gravar / Live                                                       */
/* ------------------------------------------------------------------ */

void VerticalDock::ToggleRecord()
{
	lastMessage.clear();
	if (canvas.IsRecording()) {
		canvas.StopRecording();
	} else {
		QDir().mkpath(QString::fromStdString(settings.recordDir));
		const QString path =
			QString::fromStdString(settings.recordDir) +
			"/Vertical_" +
			QDateTime::currentDateTime().toString(
				"yyyy-MM-dd_HH-mm-ss") +
			".mkv";
		std::string err;
		if (!canvas.StartRecording(path.toStdString(), settings.videoKbps,
					   settings.audioKbps, err))
			lastMessage = U8("Gravação: ") + QString::fromUtf8(err.c_str());
	}
	UpdateButtons();
}

void VerticalDock::ToggleStream()
{
	lastMessage.clear();
	if (canvas.IsStreaming()) {
		canvas.StopStreaming();
	} else {
		if (settings.server.empty()) {
			QMessageBox::information(
				this, U8("Vertical Canvas"),
				U8("Informe o servidor e a chave da live em Configurações."));
			OpenSettings();
			if (settings.server.empty())
				return;
		}
		std::string err;
		if (!canvas.StartStreaming(settings.server, settings.key,
					   settings.videoKbps,
					   settings.audioKbps, err))
			lastMessage = U8("Live: ") + QString::fromUtf8(err.c_str());
	}
	UpdateButtons();
}

void VerticalDock::PollOutputs()
{
	const std::string msg = canvas.Poll();
	if (!msg.empty())
		lastMessage = QString::fromUtf8(msg.c_str());
	UpdateButtons();
}

void VerticalDock::UpdateButtons()
{
	recBtn->setText(canvas.IsRecording() ? U8("Parar gravação")
					     : U8("Gravar"));
	streamBtn->setText(canvas.IsStreaming() ? U8("Parar live")
						: U8("Iniciar live"));

	QStringList st;
	if (canvas.IsRecording())
		st << U8("● Gravando");
	if (canvas.IsStreaming())
		st << U8("● Ao vivo");
	statusLabel->setText(st.isEmpty() ? lastMessage : st.join("   "));
}
