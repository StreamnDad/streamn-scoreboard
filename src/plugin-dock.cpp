#include "scoreboard-dock.h"
#include "plugin-version.h"
#include "../data/streamn-dad-logo.h"

#include <obs-frontend-api.h>
#include <util/config-file.h>

#include <QtCore/QByteArray>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonValue>
#include <QtCore/QProcess>
#include <QtCore/QProcessEnvironment>
#include <QtCore/QStringList>
#include <QtCore/QTextStream>
#include <QtCore/QTimer>
#include <QtGui/QClipboard>
#include <QtGui/QGuiApplication>
#include <QtGui/QPixmap>

#include <QtWidgets/QAction>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QDialog>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QMenu>
#include <QtWidgets/QPlainTextEdit>
#include <QtWidgets/QProgressBar>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QScrollArea>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QToolButton>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

namespace {
const char *kDockId = "streamn-obs-scoreboard-dock";
const char *kDockTitle = "Streamn Scoreboard";

const char *kConfigSection = "streamn-obs-scoreboard";
const char *kOutputDirKey = "output_directory";
const char *kCliExecutableKey = "cli_executable";
const char *kMainConfigKey = "main_config_path";
const char *kOverrideConfigKey = "override_config_path";
const char *kEnvFileKey = "environment_file";
const char *kAutoHighlightsKey = "auto_highlights";

enum scoreboard_event_type {
	SB_EVENT_NONE = 0,
	SB_EVENT_HOME_GOAL,
	SB_EVENT_AWAY_GOAL,
	SB_EVENT_PERIOD_CHANGE,
	SB_EVENT_GAME_START,
	SB_EVENT_GAME_END,
};

struct process_job {
	int id = 0;
	QString title;
	QWidget *row = nullptr;
	QLabel *text = nullptr;
	QProgressBar *spinner = nullptr;
	QPushButton *view_logs = nullptr;
	QPushButton *copy_logs = nullptr;
	QPushButton *cancel = nullptr;
	QProcess *process = nullptr;
	QString stdout_log;
	QString stderr_log;
	bool running = false;
	bool completed = false;
};

struct cli_action_binding {
	QString command = "publish";
	QString subcommand = "trigger";
	QStringList args_template;
	QStringList extra_args;
};

struct penalty_row_widgets {
	QWidget *container = nullptr;
	QLabel *label = nullptr;
	QPushButton *clear_btn = nullptr;
	int slot = -1;
	bool home = true;
};

/* Global state */
QWidget *g_dock_widget = nullptr;
QLabel *g_clock_label = nullptr;
QLabel *g_period_label = nullptr;
QLineEdit *g_home_name_edit = nullptr;
QLineEdit *g_away_name_edit = nullptr;
QLabel *g_home_score_label = nullptr;
QLabel *g_away_score_label = nullptr;
QLabel *g_home_shots_label = nullptr;
QLabel *g_away_shots_label = nullptr;
QVBoxLayout *g_home_pen_layout = nullptr;
QVBoxLayout *g_away_pen_layout = nullptr;
QVector<penalty_row_widgets *> g_home_pen_rows;
QVector<penalty_row_widgets *> g_away_pen_rows;
QVBoxLayout *g_queue_layout = nullptr;
QWidget *g_queue_container = nullptr;
QLabel *g_queue_empty_label = nullptr;
QPushButton *g_clock_btn = nullptr;
QTimer *g_tick_timer = nullptr;
scoreboard_log_fn g_log_fn = nullptr;

QVector<process_job *> g_jobs;
int g_next_job_id = 1;
QHash<int, cli_action_binding> g_cli_actions;
QString g_environment_file;
QPushButton *g_highlights_btn = nullptr;
QProcess *g_highlights_process = nullptr;
bool g_auto_highlights = false;

obs_hotkey_id g_hotkey_ids[23];

void log_info(const QString &message)
{
	if (g_log_fn != nullptr) {
		const QByteArray bytes = message.toUtf8();
		g_log_fn(SCOREBOARD_LOG_INFO, bytes.constData());
	}
}

QString expand_user_path(const QString &path)
{
	if (path.startsWith("~/"))
		return QDir::homePath() + path.mid(1);
	if (path == "~")
		return QDir::homePath();
	return path;
}

QString event_name(scoreboard_event_type event)
{
	switch (event) {
	case SB_EVENT_HOME_GOAL:
		return "HOME_GOAL";
	case SB_EVENT_AWAY_GOAL:
		return "AWAY_GOAL";
	case SB_EVENT_PERIOD_CHANGE:
		return "PERIOD_CHANGE";
	case SB_EVENT_GAME_START:
		return "GAME_START";
	case SB_EVENT_GAME_END:
		return "GAME_END";
	default:
		return "NONE";
	}
}

scoreboard_event_type event_from_name(const QString &name)
{
	const QString n = name.trimmed().toUpper();
	if (n == "HOME_GOAL")
		return SB_EVENT_HOME_GOAL;
	if (n == "AWAY_GOAL")
		return SB_EVENT_AWAY_GOAL;
	if (n == "PERIOD_CHANGE")
		return SB_EVENT_PERIOD_CHANGE;
	if (n == "GAME_START")
		return SB_EVENT_GAME_START;
	if (n == "GAME_END")
		return SB_EVENT_GAME_END;
	return SB_EVENT_NONE;
}

QString expand_scoreboard_token(const QString &arg, const QString &event_str)
{
	char buf[64];
	QString expanded = arg;
	expanded.replace("{event}", event_str);
	expanded.replace("{home_name}",
			 QString::fromUtf8(scoreboard_get_home_name()));
	expanded.replace("{away_name}",
			 QString::fromUtf8(scoreboard_get_away_name()));
	snprintf(buf, sizeof(buf), "%d", scoreboard_get_home_score());
	expanded.replace("{home_score}", QString::fromUtf8(buf));
	snprintf(buf, sizeof(buf), "%d", scoreboard_get_away_score());
	expanded.replace("{away_score}", QString::fromUtf8(buf));
	scoreboard_format_period(buf, sizeof(buf));
	expanded.replace("{period}", QString::fromUtf8(buf));
	scoreboard_clock_format(buf, sizeof(buf));
	expanded.replace("{clock}", QString::fromUtf8(buf));
	snprintf(buf, sizeof(buf), "%d", scoreboard_get_home_shots());
	expanded.replace("{home_shots}", QString::fromUtf8(buf));
	snprintf(buf, sizeof(buf), "%d", scoreboard_get_away_shots());
	expanded.replace("{away_shots}", QString::fromUtf8(buf));
	return expanded;
}

QString strip_shell_quotes(QString value)
{
	value = value.trimmed();
	if (value.size() >= 2) {
		const QChar first = value.front();
		const QChar last = value.back();
		if ((first == '\'' && last == '\'') ||
		    (first == '"' && last == '"'))
			value = value.mid(1, value.size() - 2);
	}
	return value;
}

QMap<QString, QString> parse_env_file(const QString &path)
{
	QMap<QString, QString> values;
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
		return values;
	QTextStream in(&file);
	while (!in.atEnd()) {
		QString line = in.readLine().trimmed();
		if (line.isEmpty() || line.startsWith('#'))
			continue;
		if (line.startsWith("export "))
			line = line.mid(7).trimmed();
		const int eq = line.indexOf('=');
		if (eq <= 0)
			continue;
		const QString key = line.left(eq).trimmed();
		QString val = line.mid(eq + 1).trimmed();
		if (key.isEmpty())
			continue;
		values.insert(key, strip_shell_quotes(val));
	}
	return values;
}

QString merge_path_value(const QString &current_path,
			 const QString &override_path)
{
	QString merged = override_path;
	merged.replace("$PATH", current_path);
	merged.replace("${PATH}", current_path);
#ifdef _WIN32
	const QChar sep = ';';
	QStringList parts = merged.split(sep, Qt::SkipEmptyParts);
	const QString sys_root =
		QProcessEnvironment::systemEnvironment().value("SystemRoot",
							       "C:\\Windows");
	const QStringList required = {sys_root + "\\system32",
				      sys_root,
				      sys_root + "\\System32\\Wbem"};
#else
	const QChar sep = ':';
	QStringList parts = merged.split(sep, Qt::SkipEmptyParts);
	const QStringList required = {"/usr/bin", "/bin", "/usr/sbin",
				      "/sbin"};
#endif
	for (const QString &entry : required) {
		if (!parts.contains(entry))
			parts << entry;
	}
	return parts.join(sep);
}

QStringList json_string_list(const QJsonValue &value)
{
	QStringList out;
	if (!value.isArray())
		return out;
	const QJsonArray arr = value.toArray();
	for (const QJsonValue &item : arr) {
		if (item.isString())
			out << item.toString();
	}
	return out;
}

QJsonObject read_json_object(const QString &path, bool *ok_out,
			     QString *error_out)
{
	QFile file(path);
	if (!file.exists()) {
		if (error_out)
			*error_out = QString("File not found: ") + path;
		if (ok_out)
			*ok_out = false;
		return QJsonObject();
	}
	if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
		if (error_out)
			*error_out = QString("Cannot open: ") + path;
		if (ok_out)
			*ok_out = false;
		return QJsonObject();
	}
	const QByteArray bytes = file.readAll();
	QJsonParseError parse_error;
	const QJsonDocument doc = QJsonDocument::fromJson(bytes, &parse_error);
	if (parse_error.error != QJsonParseError::NoError || !doc.isObject()) {
		if (error_out)
			*error_out = QString("Invalid JSON in: ") + path;
		if (ok_out)
			*ok_out = false;
		return QJsonObject();
	}
	if (ok_out)
		*ok_out = true;
	return doc.object();
}

QJsonValue merge_json_values(const QJsonValue &base_value,
			     const QJsonValue &override_value)
{
	if (override_value.isUndefined() || override_value.isNull())
		return base_value;
	if (base_value.isObject() && override_value.isObject()) {
		QJsonObject merged = base_value.toObject();
		const QJsonObject override_obj = override_value.toObject();
		for (auto it = override_obj.begin(); it != override_obj.end();
		     ++it)
			merged.insert(it.key(), merge_json_values(
							 merged.value(it.key()),
							 it.value()));
		return merged;
	}
	return override_value;
}

void load_cli_actions_from_config(const QJsonObject &merged_obj)
{
	g_cli_actions.clear();
	const QJsonObject infrastructure_obj =
		merged_obj.value("infrastructure").toObject();
	const QJsonObject obs_obj =
		infrastructure_obj.value("obs").toObject();
	QJsonObject actions_obj = obs_obj.value("event_actions").toObject();
	if (actions_obj.isEmpty())
		actions_obj = obs_obj.value("actions").toObject();
	if (actions_obj.isEmpty()) {
		const QJsonObject plugin_obj =
			merged_obj.value("obs_plugin").toObject();
		actions_obj = plugin_obj.value("actions").toObject();
	}

	for (auto it = actions_obj.begin(); it != actions_obj.end(); ++it) {
		const scoreboard_event_type event = event_from_name(it.key());
		if (event == SB_EVENT_NONE)
			continue;
		cli_action_binding action;
		const QJsonValue value = it.value();
		if (value.isArray()) {
			const QStringList argv = json_string_list(value);
			if (argv.isEmpty())
				continue;
			action.command = argv.first();
			action.subcommand.clear();
			int arg_start = 1;
			if (argv.size() > 1) {
				const QString second = argv[1].trimmed();
				if (!second.isEmpty() &&
				    !second.startsWith("-")) {
					action.subcommand = second;
					arg_start = 2;
				}
			}
			for (int i = arg_start; i < argv.size(); i++)
				action.args_template << argv[i];
			g_cli_actions.insert(static_cast<int>(event), action);
			continue;
		}
		if (!value.isObject())
			continue;
		const QJsonObject action_obj = value.toObject();
		const QString configured_command =
			action_obj.value("command").toString().trimmed();
		if (!configured_command.isEmpty())
			action.command = configured_command;
		if (action_obj.contains("subcommand"))
			action.subcommand =
				action_obj.value("subcommand")
					.toString()
					.trimmed();
		action.args_template =
			json_string_list(action_obj.value("args"));
		action.extra_args =
			json_string_list(action_obj.value("extra_args"));
		g_cli_actions.insert(static_cast<int>(event), action);
	}
}

/* ---- Process queue ---- */

void refresh_queue_placeholder()
{
	if (!g_queue_empty_label)
		return;
	bool has_rows = false;
	for (process_job *job : g_jobs) {
		if (job && job->row) {
			has_rows = true;
			break;
		}
	}
	g_queue_empty_label->setVisible(!has_rows);
}

void complete_job(process_job *job, const QString &status)
{
	if (!job)
		return;
	job->running = false;
	job->completed = true;
	if (job->spinner)
		job->spinner->hide();
	if (job->cancel)
		job->cancel->hide();
	if (job->text)
		job->text->setText(job->title + QString(" - ") + status);
	refresh_queue_placeholder();
}

QString combined_job_logs(const process_job *job)
{
	QStringList out;
	if (!job)
		return QString();
	if (!job->stdout_log.trimmed().isEmpty()) {
		out << "STDOUT:";
		out << job->stdout_log.trimmed();
	}
	if (!job->stderr_log.trimmed().isEmpty()) {
		out << "STDERR:";
		out << job->stderr_log.trimmed();
	}
	if (out.isEmpty())
		return "(no process output)";
	return out.join("\n");
}

void append_job_output(process_job *job, const QByteArray &bytes,
		       bool is_stderr)
{
	if (!job || bytes.isEmpty())
		return;
	const QString text = QString::fromUtf8(bytes);
	if (is_stderr)
		job->stderr_log += text;
	else
		job->stdout_log += text;
}

void capture_remaining_process_output(process_job *job)
{
	if (!job || !job->process)
		return;
	append_job_output(job, job->process->readAllStandardOutput(), false);
	append_job_output(job, job->process->readAllStandardError(), true);
}

void open_job_log_dialog(process_job *job)
{
	if (!job || !g_dock_widget)
		return;
	QDialog dialog(g_dock_widget);
	dialog.setWindowTitle("CLI Output");
	dialog.resize(760, 420);
	QVBoxLayout *layout = new QVBoxLayout(&dialog);
	QLabel *title = new QLabel(job->title, &dialog);
	title->setWordWrap(true);
	layout->addWidget(title);
	QPlainTextEdit *output = new QPlainTextEdit(&dialog);
	output->setReadOnly(true);
	output->setPlainText(combined_job_logs(job));
	layout->addWidget(output, 1);
	QDialogButtonBox *buttons =
		new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
	QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog,
			 &QDialog::reject);
	layout->addWidget(buttons);
	dialog.exec();
}

void copy_job_logs(process_job *job)
{
	if (!job)
		return;
	if (QClipboard *clipboard = QGuiApplication::clipboard())
		clipboard->setText(combined_job_logs(job));
}

void start_job_process(process_job *job, const QStringList &args)
{
	if (!job)
		return;
	const QString executable =
		QString::fromUtf8(scoreboard_get_cli_executable()).trimmed();
	if (executable.isEmpty()) {
		complete_job(job, "failed (no CLI configured)");
		return;
	}
	job->process = new QProcess(g_dock_widget);
	job->running = true;
	job->completed = false;
	job->process->setProgram(executable);
	job->process->setArguments(args);
	QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
	const QString env_file_path = expand_user_path(g_environment_file);
	if (!env_file_path.trimmed().isEmpty()) {
		const QMap<QString, QString> overrides =
			parse_env_file(env_file_path);
		for (auto it = overrides.begin(); it != overrides.end(); ++it) {
			if (it.key() == "PATH") {
				env.insert("PATH",
					   merge_path_value(env.value("PATH"),
							    it.value()));
			} else {
				env.insert(it.key(), it.value());
			}
		}
	}
	job->process->setProcessEnvironment(env);
	QObject::connect(
		job->process, &QProcess::readyReadStandardOutput, [job]() {
			append_job_output(
				job, job->process->readAllStandardOutput(),
				false);
		});
	QObject::connect(
		job->process, &QProcess::readyReadStandardError, [job]() {
			append_job_output(
				job, job->process->readAllStandardError(), true);
		});
	QObject::connect(
		job->process, &QProcess::errorOccurred,
		[job](QProcess::ProcessError error) {
			capture_remaining_process_output(job);
			QString status = "failed to start";
			if (error == QProcess::FailedToStart)
				status =
					"failed to start (check CLI executable)";
			complete_job(job, status);
		});
	QObject::connect(
		job->process,
		qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
		[job](int exit_code, QProcess::ExitStatus status) {
			capture_remaining_process_output(job);
			if (status == QProcess::NormalExit && exit_code == 0) {
				complete_job(job, "completed");
			} else {
				complete_job(
					job,
					QString("failed (exit=") +
						QString::number(exit_code) +
						QString(")"));
			}
		});
	job->process->start();
}

void add_job_row(const QString &title, const QStringList &args)
{
	if (!g_queue_layout)
		return;
	process_job *job = new process_job();
	job->id = g_next_job_id++;
	job->title = title;
	job->row = new QWidget(g_queue_container);
	QVBoxLayout *layout = new QVBoxLayout(job->row);
	layout->setContentsMargins(4, 4, 4, 4);
	layout->setSpacing(6);
	job->spinner = new QProgressBar(job->row);
	job->spinner->setRange(0, 0);
	job->spinner->setFixedWidth(80);
	job->spinner->setTextVisible(false);
	job->text = new QLabel(title + QString(" - running"), job->row);
	job->text->setWordWrap(true);
	job->view_logs = new QPushButton("View Logs", job->row);
	job->view_logs->setMinimumWidth(96);
	job->copy_logs = new QPushButton("Copy Logs", job->row);
	job->copy_logs->setMinimumWidth(96);
	job->cancel = new QPushButton("Cancel", job->row);
	job->cancel->setMinimumWidth(80);
	QHBoxLayout *controls = new QHBoxLayout();
	controls->setContentsMargins(0, 0, 0, 0);
	controls->setSpacing(8);
	controls->addWidget(job->spinner);
	controls->addStretch(1);
	controls->addWidget(job->view_logs);
	controls->addWidget(job->copy_logs);
	controls->addWidget(job->cancel);
	layout->addWidget(job->text);
	layout->addLayout(controls);
	QObject::connect(job->view_logs, &QPushButton::clicked,
			 [job]() { open_job_log_dialog(job); });
	QObject::connect(job->copy_logs, &QPushButton::clicked,
			 [job]() { copy_job_logs(job); });
	QObject::connect(job->cancel, &QPushButton::clicked, [job]() {
		if (job->process && job->running) {
			job->text->setText(job->title +
					   QString(" - cancelling"));
			job->cancel->setEnabled(false);
			job->process->kill();
		}
	});
	g_queue_layout->insertWidget(g_queue_layout->count() - 1, job->row);
	g_jobs.push_back(job);
	refresh_queue_placeholder();
	start_job_process(job, args);
}


void fire_cli_event(scoreboard_event_type event)
{
	if (!g_cli_actions.contains(static_cast<int>(event)))
		return;
	const cli_action_binding action =
		g_cli_actions.value(static_cast<int>(event));
	const QString event_str = event_name(event);
	QStringList args;
	args << action.command;
	if (!action.subcommand.isEmpty())
		args << action.subcommand;
	if (!action.args_template.isEmpty()) {
		for (const QString &arg : action.args_template)
			args << expand_scoreboard_token(arg, event_str);
	} else {
		args << "--event" << event_str;
	}
	add_job_row(event_str, args);
}

/* ---- UI update ---- */

void update_all_labels()
{
	char buf[64];
	if (g_clock_label) {
		scoreboard_clock_format(buf, sizeof(buf));
		g_clock_label->setText(QString::fromUtf8(buf));
	}
	if (g_clock_btn) {
		bool running = scoreboard_clock_is_running();
		g_clock_btn->setText(running ? "Stop" : "Start");
		if (running) {
			QPalette p = g_clock_btn->palette();
			QColor base = p.color(QPalette::Button);
			QColor red = QColor::fromHslF(0.0, 0.7,
						      base.lightnessF());
			g_clock_btn->setStyleSheet(
				"background-color: " + red.name()
				+ "; color: "
				+ p.color(QPalette::BrightText).name()
				+ ";");
		} else {
			g_clock_btn->setStyleSheet("");
		}
	}
	if (g_period_label) {
		scoreboard_format_period(buf, sizeof(buf));
		g_period_label->setText(QString("Period: ") +
					QString::fromUtf8(buf));
	}
	if (g_home_name_edit && !g_home_name_edit->hasFocus())
		g_home_name_edit->setText(
			QString::fromUtf8(scoreboard_get_home_name()));
	if (g_away_name_edit && !g_away_name_edit->hasFocus())
		g_away_name_edit->setText(
			QString::fromUtf8(scoreboard_get_away_name()));
	if (g_home_score_label)
		g_home_score_label->setText(
			QString::number(scoreboard_get_home_score()));
	if (g_away_score_label)
		g_away_score_label->setText(
			QString::number(scoreboard_get_away_score()));
	if (g_home_shots_label)
		g_home_shots_label->setText(
			QString::number(scoreboard_get_home_shots()));
	if (g_away_shots_label)
		g_away_shots_label->setText(
			QString::number(scoreboard_get_away_shots()));

	auto update_pen_rows = [](QVBoxLayout *layout,
				  QVector<penalty_row_widgets *> &rows,
				  bool home) {
		if (!layout)
			return;

		/* Build list of currently active slots */
		QVector<int> active_slots;
		for (int i = 0; i < SCOREBOARD_MAX_PENALTIES; i++) {
			const struct scoreboard_penalty *p =
				home ? scoreboard_get_home_penalty(i)
				     : scoreboard_get_away_penalty(i);
			if (p && p->active)
				active_slots.push_back(i);
		}

		/* Check if existing rows match active slots */
		bool need_rebuild = (rows.size() != active_slots.size());
		if (!need_rebuild) {
			for (int j = 0; j < rows.size(); j++) {
				if (rows[j]->slot != active_slots[j]) {
					need_rebuild = true;
					break;
				}
			}
		}

		/* If slots haven't changed, just update the label text */
		if (!need_rebuild) {
			for (penalty_row_widgets *pw : rows) {
				char nbuf[32], tbuf[32];
				scoreboard_format_penalty_number(
					pw->slot, pw->home, nbuf,
					sizeof(nbuf));
				scoreboard_format_penalty_time(
					pw->slot, pw->home, tbuf,
					sizeof(tbuf));
				pw->label->setText(
					QString::fromUtf8(nbuf) + " " +
					QString::fromUtf8(tbuf));
			}
			return;
		}

		/* Active set changed — rebuild rows */
		for (penalty_row_widgets *row : rows) {
			layout->removeWidget(row->container);
			row->container->hide();
			row->container->deleteLater();
			delete row;
		}
		rows.clear();

		for (int i : active_slots) {
			char nbuf[32], tbuf[32];
			scoreboard_format_penalty_number(i, home, nbuf,
							 sizeof(nbuf));
			scoreboard_format_penalty_time(i, home, tbuf,
						       sizeof(tbuf));

			penalty_row_widgets *pw = new penalty_row_widgets();
			pw->slot = i;
			pw->home = home;
			pw->container = new QWidget();
			QHBoxLayout *hl = new QHBoxLayout(pw->container);
			hl->setContentsMargins(0, 0, 0, 0);
			hl->setSpacing(4);

			pw->label = new QLabel(
				QString::fromUtf8(nbuf) + " " +
				QString::fromUtf8(tbuf));
			pw->label->setStyleSheet("font-size: 11px;");
			pw->clear_btn = new QPushButton("X");
			pw->clear_btn->setFixedSize(18, 18);
			pw->clear_btn->setStyleSheet(
				"QPushButton { padding: 0px; min-height: 16px; max-height: 18px; font-size: 10px; }");

			int captured_slot = i;
			bool captured_home = home;
			QObject::connect(
				pw->clear_btn, &QPushButton::clicked,
				[captured_slot, captured_home]() {
					if (captured_home)
						scoreboard_home_penalty_clear(
							captured_slot);
					else
						scoreboard_away_penalty_clear(
							captured_slot);
					scoreboard_write_all_files();
					update_all_labels();
				});

			hl->addWidget(pw->label, 1);
			hl->addWidget(pw->clear_btn);
			layout->addWidget(pw->container);
			rows.push_back(pw);
		}
	};

	update_pen_rows(g_home_pen_layout, g_home_pen_rows, true);
	update_pen_rows(g_away_pen_layout, g_away_pen_rows, false);
}

void on_tick()
{
	scoreboard_clock_tick(1);
	scoreboard_write_all_files();
	update_all_labels();
}

/* ---- Profile paths ---- */

void load_profile_paths()
{
	config_t *profile_cfg = obs_frontend_get_profile_config();
	const char *output_dir = nullptr;
	const char *cli_exe = nullptr;
	const char *main_cfg = nullptr;
	const char *override_cfg = nullptr;
	const char *env_file = nullptr;
	const char *auto_hl = nullptr;

	if (profile_cfg != nullptr) {
		output_dir = config_get_string(profile_cfg, kConfigSection,
					       kOutputDirKey);
		cli_exe = config_get_string(profile_cfg, kConfigSection,
					    kCliExecutableKey);
		main_cfg = config_get_string(profile_cfg, kConfigSection,
					     kMainConfigKey);
		override_cfg = config_get_string(profile_cfg, kConfigSection,
						 kOverrideConfigKey);
		env_file = config_get_string(profile_cfg, kConfigSection,
					     kEnvFileKey);
		auto_hl = config_get_string(profile_cfg, kConfigSection,
					    kAutoHighlightsKey);
	}

	scoreboard_set_output_directory(output_dir);
	scoreboard_set_cli_executable(cli_exe);
	scoreboard_set_main_config_path(main_cfg);
	scoreboard_set_override_config_path(override_cfg);
	g_environment_file =
		env_file ? QString::fromUtf8(env_file).trimmed() : QString();
	g_auto_highlights =
		auto_hl ? QString::fromUtf8(auto_hl).trimmed() == "true"
			: false;
}

void save_profile_paths()
{
	config_t *profile_cfg = obs_frontend_get_profile_config();
	if (profile_cfg == nullptr)
		return;
	config_set_string(profile_cfg, kConfigSection, kOutputDirKey,
			  scoreboard_get_output_directory());
	config_set_string(profile_cfg, kConfigSection, kCliExecutableKey,
			  scoreboard_get_cli_executable());
	config_set_string(profile_cfg, kConfigSection, kMainConfigKey,
			  scoreboard_get_main_config_path());
	config_set_string(profile_cfg, kConfigSection, kOverrideConfigKey,
			  scoreboard_get_override_config_path());
	config_set_string(profile_cfg, kConfigSection, kEnvFileKey,
			  g_environment_file.toUtf8().constData());
	config_set_string(profile_cfg, kConfigSection, kAutoHighlightsKey,
			  g_auto_highlights ? "true" : "false");
	config_save_safe(profile_cfg, "tmp", nullptr);
}

void load_cli_config()
{
	const QString main_path = expand_user_path(
		QString::fromUtf8(scoreboard_get_main_config_path()));
	const QString override_path = expand_user_path(
		QString::fromUtf8(scoreboard_get_override_config_path()));
	bool main_ok = false;
	bool override_ok = false;
	const QJsonObject main_obj =
		read_json_object(main_path, &main_ok, nullptr);
	const QJsonObject override_obj =
		read_json_object(override_path, &override_ok, nullptr);
	QJsonObject merged_obj;
	if (main_ok || override_ok)
		merged_obj =
			merge_json_values(main_obj, override_obj).toObject();
	load_cli_actions_from_config(merged_obj);
}

/* ---- Highlights generation ---- */

void update_highlights_button_visibility()
{
	if (!g_highlights_btn)
		return;
	const QString cli_path =
		QString::fromUtf8(scoreboard_get_cli_executable()).trimmed();
	g_highlights_btn->setVisible(!cli_path.isEmpty());
}

void finish_highlights_generation()
{
	if (g_highlights_process) {
		g_highlights_process->deleteLater();
		g_highlights_process = nullptr;
	}
	if (g_highlights_btn) {
		g_highlights_btn->setText("Generate Period Highlights");
		g_highlights_btn->setStyleSheet("");
		g_highlights_btn->setEnabled(true);
	}
}

void start_highlights_generation()
{
	const QString executable =
		QString::fromUtf8(scoreboard_get_cli_executable()).trimmed();
	if (executable.isEmpty())
		return;
	if (g_highlights_process)
		return;

	g_highlights_process = new QProcess(g_dock_widget);
	g_highlights_process->setProgram(executable);
	g_highlights_process->setArguments({"publish", "highlights"});

	QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
	const QString env_file_path = expand_user_path(g_environment_file);
	if (!env_file_path.trimmed().isEmpty()) {
		const QMap<QString, QString> overrides =
			parse_env_file(env_file_path);
		for (auto it = overrides.begin(); it != overrides.end(); ++it) {
			if (it.key() == "PATH") {
				env.insert("PATH",
					   merge_path_value(env.value("PATH"),
							    it.value()));
			} else {
				env.insert(it.key(), it.value());
			}
		}
	}
	g_highlights_process->setProcessEnvironment(env);

	QObject::connect(
		g_highlights_process, &QProcess::errorOccurred,
		[](QProcess::ProcessError) { finish_highlights_generation(); });
	QObject::connect(
		g_highlights_process,
		qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
		[](int, QProcess::ExitStatus) {
			finish_highlights_generation();
		});

	if (g_highlights_btn) {
		g_highlights_btn->setText("Generating...");
		g_highlights_btn->setEnabled(false);
		QPalette p = g_highlights_btn->palette();
		QColor base = p.color(QPalette::Button);
		QColor red = QColor::fromHslF(0.0, 0.7, base.lightnessF());
		g_highlights_btn->setStyleSheet(
			"color: " + red.name() + "; font-weight: bold;");
	}

	g_highlights_process->start();
	log_info("[streamn-obs-scoreboard] highlights generation started");
}

/* ---- Dialogs ---- */

void open_add_penalty_dialog(QWidget *parent, bool home)
{
	QDialog dialog(parent);
	dialog.setWindowTitle(home ? "Add Home Penalty" : "Add Away Penalty");
	QVBoxLayout *layout = new QVBoxLayout(&dialog);

	QHBoxLayout *num_row = new QHBoxLayout();
	num_row->addWidget(new QLabel("Player #:", &dialog));
	QLineEdit *num_input = new QLineEdit(&dialog);
	num_input->setPlaceholderText("optional");
	num_row->addWidget(num_input);
	layout->addLayout(num_row);

	QHBoxLayout *dur_row = new QHBoxLayout();
	dur_row->addWidget(new QLabel("Duration (sec):", &dialog));
	QSpinBox *dur_spin = new QSpinBox(&dialog);
	dur_spin->setRange(1, 600);
	dur_spin->setValue(scoreboard_get_default_penalty_duration());
	dur_row->addWidget(dur_spin);
	layout->addLayout(dur_row);

	QDialogButtonBox *buttons = new QDialogButtonBox(
		QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
	QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog,
			 &QDialog::accept);
	QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog,
			 &QDialog::reject);
	layout->addWidget(buttons);

	if (dialog.exec() == QDialog::Accepted) {
		bool ok = false;
		int player_num = num_input->text().trimmed().toInt(&ok);
		if (!ok)
			player_num = 0;
		int slot;
		if (home)
			slot = scoreboard_home_penalty_add(player_num,
							   dur_spin->value());
		else
			slot = scoreboard_away_penalty_add(player_num,
							   dur_spin->value());
		if (slot < 0)
			log_info("[streamn-obs-scoreboard] penalty slots full");
		update_all_labels();
	}
}

void open_configure_dialog(QWidget *parent)
{
	QDialog dialog(parent);
	dialog.setWindowTitle("Streamn Scoreboard Settings");
	QVBoxLayout *root = new QVBoxLayout(&dialog);
	QGridLayout *grid = new QGridLayout();

	QLabel *out_label = new QLabel("Output directory", &dialog);
	QLineEdit *out_input = new QLineEdit(&dialog);
	QPushButton *out_browse = new QPushButton("Browse", &dialog);

	out_input->setText(
		QString::fromUtf8(scoreboard_get_output_directory()));

	grid->addWidget(out_label, 0, 0);
	grid->addWidget(out_input, 0, 1);
	grid->addWidget(out_browse, 0, 2);
	root->addLayout(grid);

	QDialogButtonBox *button_box = new QDialogButtonBox(
		QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
	root->addWidget(button_box);

	QObject::connect(out_browse, &QPushButton::clicked,
			 [&dialog, out_input]() {
				 const QString dir =
					 QFileDialog::getExistingDirectory(
						 &dialog,
						 "Select Output Directory",
						 out_input->text());
				 if (!dir.isEmpty())
					 out_input->setText(dir);
			 });
	QObject::connect(button_box, &QDialogButtonBox::accepted, &dialog,
			 &QDialog::accept);
	QObject::connect(button_box, &QDialogButtonBox::rejected, &dialog,
			 &QDialog::reject);

	if (dialog.exec() == QDialog::Accepted) {
		scoreboard_set_output_directory(
			out_input->text().trimmed().toUtf8().constData());
		save_profile_paths();
		update_all_labels();
	}
}

void open_clock_settings_dialog(QWidget *parent)
{
	QDialog dialog(parent);
	dialog.setWindowTitle("Game Settings");
	QVBoxLayout *layout = new QVBoxLayout(&dialog);

	QHBoxLayout *len_row = new QHBoxLayout();
	len_row->addWidget(new QLabel("Period length (minutes):", &dialog));
	QSpinBox *len_spin = new QSpinBox(&dialog);
	len_spin->setRange(1, 60);
	len_spin->setValue(scoreboard_get_period_length() / 60);
	len_row->addWidget(len_spin);
	layout->addLayout(len_row);

	QHBoxLayout *dir_row = new QHBoxLayout();
	dir_row->addWidget(new QLabel("Direction:", &dialog));
	QPushButton *down_btn = new QPushButton("Count Down", &dialog);
	QPushButton *up_btn = new QPushButton("Count Up", &dialog);
	down_btn->setCheckable(true);
	up_btn->setCheckable(true);
	if (scoreboard_get_clock_direction() == SCOREBOARD_CLOCK_COUNT_DOWN)
		down_btn->setChecked(true);
	else
		up_btn->setChecked(true);
	dir_row->addWidget(down_btn);
	dir_row->addWidget(up_btn);
	layout->addLayout(dir_row);

	QObject::connect(down_btn, &QPushButton::clicked, [down_btn, up_btn]() {
		down_btn->setChecked(true);
		up_btn->setChecked(false);
	});
	QObject::connect(up_btn, &QPushButton::clicked, [down_btn, up_btn]() {
		up_btn->setChecked(true);
		down_btn->setChecked(false);
	});

	QHBoxLayout *pen_dur_row = new QHBoxLayout();
	pen_dur_row->addWidget(
		new QLabel("Default penalty (seconds):", &dialog));
	QSpinBox *pen_dur_spin = new QSpinBox(&dialog);
	pen_dur_spin->setRange(1, 600);
	pen_dur_spin->setValue(scoreboard_get_default_penalty_duration());
	pen_dur_row->addWidget(pen_dur_spin);
	layout->addLayout(pen_dur_row);

	QFrame *sep = new QFrame(&dialog);
	sep->setFrameShape(QFrame::HLine);
	sep->setFrameShadow(QFrame::Sunken);
	layout->addWidget(sep);

	QHBoxLayout *cli_row = new QHBoxLayout();
	cli_row->addWidget(new QLabel("Streamn CLI path:", &dialog));
	QLineEdit *cli_input = new QLineEdit(&dialog);
	cli_input->setText(
		QString::fromUtf8(scoreboard_get_cli_executable()));
	cli_input->setPlaceholderText("/path/to/streamn-cli");
	QPushButton *cli_browse = new QPushButton("Browse", &dialog);
	cli_row->addWidget(cli_input, 1);
	cli_row->addWidget(cli_browse);
	layout->addLayout(cli_row);

	QObject::connect(cli_browse, &QPushButton::clicked,
			 [&dialog, cli_input]() {
				 const QString path =
					 QFileDialog::getOpenFileName(
						 &dialog,
						 "Select Streamn CLI",
						 cli_input->text());
				 if (!path.isEmpty())
					 cli_input->setText(path);
			 });

	QCheckBox *auto_hl_check =
		new QCheckBox("Auto-generate period highlights on period "
			      "change",
			      &dialog);
	auto_hl_check->setChecked(g_auto_highlights);
	layout->addWidget(auto_hl_check);

	QDialogButtonBox *buttons = new QDialogButtonBox(
		QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
	QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog,
			 &QDialog::accept);
	QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog,
			 &QDialog::reject);
	layout->addWidget(buttons);

	if (dialog.exec() == QDialog::Accepted) {
		scoreboard_set_period_length(len_spin->value() * 60);
		scoreboard_set_clock_direction(
			down_btn->isChecked() ? SCOREBOARD_CLOCK_COUNT_DOWN
					      : SCOREBOARD_CLOCK_COUNT_UP);
		scoreboard_set_default_penalty_duration(
			pen_dur_spin->value());
		scoreboard_set_cli_executable(
			cli_input->text().trimmed().toUtf8().constData());
		g_auto_highlights = auto_hl_check->isChecked();
		save_profile_paths();
		scoreboard_clock_reset();
		update_all_labels();
		update_highlights_button_visibility();
	}
}

void open_about_dialog(QWidget *parent)
{
	QDialog dialog(parent);
	dialog.setWindowTitle("About Streamn Scoreboard");
	dialog.setFixedWidth(360);
	QVBoxLayout *layout = new QVBoxLayout(&dialog);

	QLabel *title_label = new QLabel(
		"<b>Streamn Scoreboard</b> v" PLUGIN_VERSION, &dialog);
	title_label->setAlignment(Qt::AlignCenter);
	layout->addWidget(title_label);

	QLabel *desc_label = new QLabel(
		"OBS Studio plugin for tracking youth hockey scoreboard state. "
		"Writes game data to text files for use with OBS Text sources.",
		&dialog);
	desc_label->setWordWrap(true);
	desc_label->setAlignment(Qt::AlignCenter);
	layout->addWidget(desc_label);

	layout->addSpacing(8);

	QLabel *license_label = new QLabel(
		"License: <a href=\"https://www.gnu.org/licenses/old-licenses/gpl-2.0.html\">GNU GPL v2</a>",
		&dialog);
	license_label->setOpenExternalLinks(true);
	license_label->setAlignment(Qt::AlignCenter);
	layout->addWidget(license_label);

	QLabel *repo_label = new QLabel(
		"<a href=\"https://github.com/StreamnDad/streamn-scoreboard\">github.com/StreamnDad/streamn-scoreboard</a>",
		&dialog);
	repo_label->setOpenExternalLinks(true);
	repo_label->setAlignment(Qt::AlignCenter);
	layout->addWidget(repo_label);

	layout->addSpacing(12);

	QLabel *logo_label = new QLabel(&dialog);
	QPixmap logo;
	logo.loadFromData(data_streamn_dad_logo_jpg,
			  data_streamn_dad_logo_jpg_len, "JPEG");
	logo_label->setPixmap(logo.scaled(48, 48, Qt::KeepAspectRatio,
					   Qt::SmoothTransformation));
	logo_label->setAlignment(Qt::AlignCenter);
	layout->addWidget(logo_label);

	QLabel *brought_label = new QLabel(
		"Brought to you by <a href=\"https://streamn.dad\">StreaMN Dad</a>",
		&dialog);
	brought_label->setOpenExternalLinks(true);
	brought_label->setAlignment(Qt::AlignCenter);
	layout->addWidget(brought_label);

	layout->addSpacing(8);

	QDialogButtonBox *buttons =
		new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
	QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog,
			 &QDialog::reject);
	layout->addWidget(buttons);

	dialog.exec();
}

/* ---- Hotkeys ---- */

void hk_clock_startstop(void *, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (!pressed)
		return;
	if (scoreboard_clock_is_running())
		scoreboard_clock_stop();
	else
		scoreboard_clock_start();
}

void hk_clock_reset(void *, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (pressed)
		scoreboard_clock_reset();
}

void hk_clock_plus1min(void *, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (pressed)
		scoreboard_clock_adjust_minutes(1);
}

void hk_clock_minus1min(void *, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (pressed)
		scoreboard_clock_adjust_minutes(-1);
}


void hk_clock_plus1sec(void *, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (pressed)
		scoreboard_clock_adjust_seconds(1);
}

void hk_clock_minus1sec(void *, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (pressed)
		scoreboard_clock_adjust_seconds(-1);
}

void hk_home_goal_plus(void *, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (!pressed)
		return;
	scoreboard_increment_home_score();
	fire_cli_event(SB_EVENT_HOME_GOAL);
}

void hk_home_goal_minus(void *, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (pressed)
		scoreboard_decrement_home_score();
}

void hk_home_shot_plus(void *, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (pressed)
		scoreboard_increment_home_shots();
}

void hk_home_shot_minus(void *, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (pressed)
		scoreboard_decrement_home_shots();
}

void hk_away_goal_plus(void *, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (!pressed)
		return;
	scoreboard_increment_away_score();
	fire_cli_event(SB_EVENT_AWAY_GOAL);
}

void hk_away_goal_minus(void *, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (pressed)
		scoreboard_decrement_away_score();
}

void hk_away_shot_plus(void *, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (pressed)
		scoreboard_increment_away_shots();
}

void hk_away_shot_minus(void *, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (pressed)
		scoreboard_decrement_away_shots();
}

void hk_period_advance(void *, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (!pressed)
		return;
	scoreboard_period_advance();
	fire_cli_event(SB_EVENT_PERIOD_CHANGE);
	if (g_auto_highlights)
		start_highlights_generation();
}

void hk_period_rewind(void *, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (!pressed)
		return;
	scoreboard_period_rewind();
	fire_cli_event(SB_EVENT_PERIOD_CHANGE);
}

void hk_home_pen_add(void *, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (pressed)
		scoreboard_home_penalty_add(
			0, scoreboard_get_default_penalty_duration());
}

void hk_home_pen_clear1(void *, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (pressed)
		scoreboard_home_penalty_clear(0);
}

void hk_home_pen_clear2(void *, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (pressed)
		scoreboard_home_penalty_clear(1);
}

void hk_away_pen_add(void *, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (pressed)
		scoreboard_away_penalty_add(
			0, scoreboard_get_default_penalty_duration());
}

void hk_away_pen_clear1(void *, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (pressed)
		scoreboard_away_penalty_clear(0);
}

void hk_away_pen_clear2(void *, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (pressed)
		scoreboard_away_penalty_clear(1);
}

void hk_generate_highlights(void *, obs_hotkey_id, obs_hotkey_t *,
			     bool pressed)
{
	if (pressed)
		start_highlights_generation();
}

void register_hotkeys()
{
	int idx = 0;
	g_hotkey_ids[idx++] = obs_hotkey_register_frontend(
		"sb_clock_startstop", "Streamn: Clock Start/Stop",
		hk_clock_startstop, nullptr);
	g_hotkey_ids[idx++] = obs_hotkey_register_frontend(
		"sb_clock_reset", "Streamn: Clock Reset", hk_clock_reset,
		nullptr);
	g_hotkey_ids[idx++] = obs_hotkey_register_frontend(
		"sb_clock_plus1min", "Streamn: Clock +1 Min",
		hk_clock_plus1min, nullptr);
	g_hotkey_ids[idx++] = obs_hotkey_register_frontend(
		"sb_clock_minus1min", "Streamn: Clock -1 Min",
		hk_clock_minus1min, nullptr);
	g_hotkey_ids[idx++] = obs_hotkey_register_frontend(
		"sb_clock_plus1sec", "Streamn: Clock +1 Sec",
		hk_clock_plus1sec, nullptr);
	g_hotkey_ids[idx++] = obs_hotkey_register_frontend(
		"sb_clock_minus1sec", "Streamn: Clock -1 Sec",
		hk_clock_minus1sec, nullptr);
	g_hotkey_ids[idx++] = obs_hotkey_register_frontend(
		"sb_home_goal_plus", "Streamn: Home Goal +",
		hk_home_goal_plus, nullptr);
	g_hotkey_ids[idx++] = obs_hotkey_register_frontend(
		"sb_home_goal_minus", "Streamn: Home Goal -",
		hk_home_goal_minus, nullptr);
	g_hotkey_ids[idx++] = obs_hotkey_register_frontend(
		"sb_home_shot_plus", "Streamn: Home Shot +",
		hk_home_shot_plus, nullptr);
	g_hotkey_ids[idx++] = obs_hotkey_register_frontend(
		"sb_home_shot_minus", "Streamn: Home Shot -",
		hk_home_shot_minus, nullptr);
	g_hotkey_ids[idx++] = obs_hotkey_register_frontend(
		"sb_away_goal_plus", "Streamn: Away Goal +",
		hk_away_goal_plus, nullptr);
	g_hotkey_ids[idx++] = obs_hotkey_register_frontend(
		"sb_away_goal_minus", "Streamn: Away Goal -",
		hk_away_goal_minus, nullptr);
	g_hotkey_ids[idx++] = obs_hotkey_register_frontend(
		"sb_away_shot_plus", "Streamn: Away Shot +",
		hk_away_shot_plus, nullptr);
	g_hotkey_ids[idx++] = obs_hotkey_register_frontend(
		"sb_away_shot_minus", "Streamn: Away Shot -",
		hk_away_shot_minus, nullptr);
	g_hotkey_ids[idx++] = obs_hotkey_register_frontend(
		"sb_period_advance", "Streamn: Period Advance",
		hk_period_advance, nullptr);
	g_hotkey_ids[idx++] = obs_hotkey_register_frontend(
		"sb_period_rewind", "Streamn: Period Rewind",
		hk_period_rewind, nullptr);
	g_hotkey_ids[idx++] = obs_hotkey_register_frontend(
		"sb_home_pen_add", "Streamn: Home Penalty Add",
		hk_home_pen_add, nullptr);
	g_hotkey_ids[idx++] = obs_hotkey_register_frontend(
		"sb_home_pen_clear1", "Streamn: Home Penalty Clear 1",
		hk_home_pen_clear1, nullptr);
	g_hotkey_ids[idx++] = obs_hotkey_register_frontend(
		"sb_home_pen_clear2", "Streamn: Home Penalty Clear 2",
		hk_home_pen_clear2, nullptr);
	g_hotkey_ids[idx++] = obs_hotkey_register_frontend(
		"sb_away_pen_add", "Streamn: Away Penalty Add",
		hk_away_pen_add, nullptr);
	g_hotkey_ids[idx++] = obs_hotkey_register_frontend(
		"sb_away_pen_clear1", "Streamn: Away Penalty Clear 1",
		hk_away_pen_clear1, nullptr);
	g_hotkey_ids[idx++] = obs_hotkey_register_frontend(
		"sb_away_pen_clear2", "Streamn: Away Penalty Clear 2",
		hk_away_pen_clear2, nullptr);
	g_hotkey_ids[idx++] = obs_hotkey_register_frontend(
		"sb_generate_highlights",
		"Streamn: Generate Period Highlights",
		hk_generate_highlights, nullptr);
}

void on_frontend_event(enum obs_frontend_event event, void *private_data)
{
	(void)private_data;
	if (event == OBS_FRONTEND_EVENT_PROFILE_CHANGED ||
	    event == OBS_FRONTEND_EVENT_FINISHED_LOADING) {
		load_profile_paths();
		load_cli_config();
		update_all_labels();
		update_highlights_button_visibility();
	}
}
} // namespace

bool scoreboard_dock_init(scoreboard_log_fn log_fn)
{
	if (g_dock_widget != nullptr)
		return true;

	g_log_fn = log_fn;
	scoreboard_reset_state_for_tests();
	load_profile_paths();
	scoreboard_read_all_files();

	QWidget *widget = new QWidget();
	widget->setStyleSheet(
		"QPushButton { padding: 2px 6px; min-height: 20px; max-height: 24px; }"
		"QLabel { margin: 0px; padding: 0px; }");

	QVBoxLayout *root = new QVBoxLayout(widget);
	root->setContentsMargins(6, 4, 6, 4);
	root->setSpacing(2);

	/* Header */
	QHBoxLayout *header = new QHBoxLayout();
	header->setContentsMargins(0, 0, 0, 0);
	header->setSpacing(0);
	QLabel *title = new QLabel("Streamn Scoreboard", widget);
	title->setStyleSheet("font-weight: bold;");
	QString kVersionStyle = "font-size: 9px; color: "
		+ widget->palette().color(QPalette::Disabled, QPalette::WindowText).name() + ";";
	QLabel *version_label = new QLabel("v" PLUGIN_VERSION, widget);
	version_label->setStyleSheet(kVersionStyle);
	QToolButton *menu_button = new QToolButton(widget);
	QMenu *menu = new QMenu(menu_button);
	QAction *configure_action = menu->addAction("Output Directory...");
	QAction *clock_settings_action = menu->addAction("Game Settings...");
	QAction *new_game_action = menu->addAction("New Game");
	QAction *refresh_action = menu->addAction("Refresh State...");
	menu->addSeparator();
	QAction *about_action = menu->addAction("About...");

	menu_button->setText(QString::fromUtf8("\xe2\x8b\xae"));
	menu_button->setPopupMode(QToolButton::InstantPopup);
	menu_button->setToolButtonStyle(Qt::ToolButtonTextOnly);
	menu_button->setAutoRaise(true);
	menu_button->setFixedSize(20, 20);
	menu_button->setStyleSheet(
		"QToolButton { background: transparent; border: none; padding: 0px; font-size: 16px; }"
		"QToolButton::menu-indicator { image: none; width: 0px; }");
	menu_button->setMenu(menu);

	header->addWidget(title);
	header->addSpacing(4);
	header->addWidget(version_label);
	header->addStretch(1);
	header->addWidget(menu_button);
	root->addLayout(header);

	/* ---- SECTION: Clock & Period ---- */

	/* Minutes ^/v | clock display | Seconds ^/v */
	QHBoxLayout *clock_row = new QHBoxLayout();
	clock_row->setContentsMargins(0, 0, 0, 0);
	clock_row->setSpacing(2);

	/* Minutes column (left) */
	QVBoxLayout *min_col = new QVBoxLayout();
	min_col->setContentsMargins(0, 0, 0, 0);
	min_col->setSpacing(1);
	QPushButton *clock_plus_min = new QPushButton(QString::fromUtf8("\xe2\x96\xb2"), widget);
	QPushButton *clock_minus_min = new QPushButton(QString::fromUtf8("\xe2\x96\xbc"), widget);
	clock_plus_min->setFixedSize(28, 20);
	clock_minus_min->setFixedSize(28, 20);
	clock_plus_min->setToolTip("+1 min");
	clock_minus_min->setToolTip("-1 min");
	min_col->addWidget(clock_plus_min);
	min_col->addWidget(clock_minus_min);

	/* Clock display (center) */
	g_clock_label = new QLabel("15:00", widget);
	g_clock_label->setAlignment(Qt::AlignCenter);
	g_clock_label->setStyleSheet("font-size: 28px; font-weight: bold; margin: 0px; padding: 0px;");
	g_clock_label->setFixedHeight(42);

	/* Seconds column (right) — auto-repeat for hold */
	QVBoxLayout *sec_col = new QVBoxLayout();
	sec_col->setContentsMargins(0, 0, 0, 0);
	sec_col->setSpacing(1);
	QPushButton *clock_plus_sec = new QPushButton(QString::fromUtf8("\xe2\x96\xb2"), widget);
	QPushButton *clock_minus_sec = new QPushButton(QString::fromUtf8("\xe2\x96\xbc"), widget);
	clock_plus_sec->setFixedSize(28, 20);
	clock_minus_sec->setFixedSize(28, 20);
	clock_plus_sec->setToolTip("+1 sec");
	clock_minus_sec->setToolTip("-1 sec");
	clock_plus_sec->setAutoRepeat(true);
	clock_plus_sec->setAutoRepeatDelay(400);
	clock_plus_sec->setAutoRepeatInterval(80);
	clock_minus_sec->setAutoRepeat(true);
	clock_minus_sec->setAutoRepeatDelay(400);
	clock_minus_sec->setAutoRepeatInterval(80);
	sec_col->addWidget(clock_plus_sec);
	sec_col->addWidget(clock_minus_sec);

	clock_row->addLayout(min_col);
	clock_row->addWidget(g_clock_label, 1);
	clock_row->addLayout(sec_col);
	root->addLayout(clock_row);

	g_clock_btn = new QPushButton("Start", widget);
	root->addWidget(g_clock_btn);

	QHBoxLayout *period_row = new QHBoxLayout();
	period_row->setContentsMargins(0, 0, 0, 0);
	period_row->setSpacing(4);
	g_period_label = new QLabel("Period: 1", widget);
	g_period_label->setAlignment(Qt::AlignCenter);
	QPushButton *period_rew_btn = new QPushButton("<", widget);
	QPushButton *period_adv_btn = new QPushButton(">", widget);
	period_rew_btn->setFixedWidth(28);
	period_adv_btn->setFixedWidth(28);
	period_row->addWidget(period_rew_btn);
	period_row->addWidget(g_period_label, 1);
	period_row->addWidget(period_adv_btn);
	root->addLayout(period_row);

	/* ---- Separator ---- */
	auto add_separator = [&]() {
		QFrame *line = new QFrame(widget);
		line->setFrameShape(QFrame::HLine);
		line->setFrameShadow(QFrame::Sunken);
		line->setFixedHeight(1);
		root->addWidget(line);
	};
	add_separator();

	/* ---- SECTION: Score & Shots ---- */
	QHBoxLayout *team_header = new QHBoxLayout();
	team_header->setContentsMargins(0, 0, 0, 0);
	team_header->setSpacing(4);
	g_home_name_edit = new QLineEdit(
		QString::fromUtf8(scoreboard_get_home_name()), widget);
	g_away_name_edit = new QLineEdit(
		QString::fromUtf8(scoreboard_get_away_name()), widget);
	g_home_name_edit->setAlignment(Qt::AlignCenter);
	g_away_name_edit->setAlignment(Qt::AlignCenter);
	g_home_name_edit->setStyleSheet(
		"font-weight: bold; border: none; background: transparent;");
	g_away_name_edit->setStyleSheet(
		"font-weight: bold; border: none; background: transparent;");
	g_home_name_edit->setPlaceholderText("Home");
	g_away_name_edit->setPlaceholderText("Away");
	team_header->addWidget(g_home_name_edit, 1);
	team_header->addWidget(g_away_name_edit, 1);
	root->addLayout(team_header);

	/* Score row: [-] 0 [+] | [-] 0 [+] — centered, score in accent color */
	QString kMutedStyle = "font-size: 10px; color: "
		+ widget->palette().color(QPalette::Disabled, QPalette::WindowText).name() + ";";
	QString kScoreStyle = "font-size: 18px; font-weight: bold; color: "
		+ widget->palette().color(QPalette::Highlight).name() + ";";

	QHBoxLayout *score_row = new QHBoxLayout();
	score_row->setContentsMargins(0, 0, 0, 0);
	score_row->setSpacing(2);

	QPushButton *home_goal_minus = new QPushButton("-", widget);
	QPushButton *home_goal_plus = new QPushButton("+", widget);
	home_goal_minus->setFixedWidth(28);
	home_goal_plus->setFixedWidth(28);
	g_home_score_label = new QLabel("0", widget);
	g_home_score_label->setAlignment(Qt::AlignCenter);
	g_home_score_label->setStyleSheet(kScoreStyle);
	g_home_score_label->setFixedWidth(32);
	score_row->addStretch(1);
	score_row->addWidget(home_goal_minus);
	score_row->addWidget(g_home_score_label);
	score_row->addWidget(home_goal_plus);

	QLabel *score_label = new QLabel("Score", widget);
	score_label->setAlignment(Qt::AlignCenter);
	score_label->setStyleSheet(kMutedStyle);
	score_label->setFixedWidth(36);
	score_row->addSpacing(4);
	score_row->addWidget(score_label);
	score_row->addSpacing(4);

	QPushButton *away_goal_minus = new QPushButton("-", widget);
	QPushButton *away_goal_plus = new QPushButton("+", widget);
	away_goal_minus->setFixedWidth(28);
	away_goal_plus->setFixedWidth(28);
	g_away_score_label = new QLabel("0", widget);
	g_away_score_label->setAlignment(Qt::AlignCenter);
	g_away_score_label->setStyleSheet(kScoreStyle);
	g_away_score_label->setFixedWidth(32);
	score_row->addWidget(away_goal_minus);
	score_row->addWidget(g_away_score_label);
	score_row->addWidget(away_goal_plus);
	score_row->addStretch(1);
	root->addLayout(score_row);

	/* Shots row: [-] 0 [+] | [-] 0 [+] — centered */
	QHBoxLayout *shots_row = new QHBoxLayout();
	shots_row->setContentsMargins(0, 0, 0, 0);
	shots_row->setSpacing(2);

	QPushButton *home_shot_minus = new QPushButton("-", widget);
	QPushButton *home_shot_plus = new QPushButton("+", widget);
	home_shot_minus->setFixedWidth(28);
	home_shot_plus->setFixedWidth(28);
	g_home_shots_label = new QLabel("0", widget);
	g_home_shots_label->setAlignment(Qt::AlignCenter);
	g_home_shots_label->setFixedWidth(32);
	shots_row->addStretch(1);
	shots_row->addWidget(home_shot_minus);
	shots_row->addWidget(g_home_shots_label);
	shots_row->addWidget(home_shot_plus);

	QLabel *shots_label = new QLabel("SOG", widget);
	shots_label->setAlignment(Qt::AlignCenter);
	shots_label->setStyleSheet(kMutedStyle);
	shots_label->setFixedWidth(36);
	shots_row->addSpacing(4);
	shots_row->addWidget(shots_label);
	shots_row->addSpacing(4);

	QPushButton *away_shot_minus = new QPushButton("-", widget);
	QPushButton *away_shot_plus = new QPushButton("+", widget);
	away_shot_minus->setFixedWidth(28);
	away_shot_plus->setFixedWidth(28);
	g_away_shots_label = new QLabel("0", widget);
	g_away_shots_label->setAlignment(Qt::AlignCenter);
	g_away_shots_label->setFixedWidth(32);
	shots_row->addWidget(away_shot_minus);
	shots_row->addWidget(g_away_shots_label);
	shots_row->addWidget(away_shot_plus);
	shots_row->addStretch(1);
	root->addLayout(shots_row);

	add_separator();

	/* ---- SECTION: Penalties (dynamic) ---- */
	QHBoxLayout *pen_section = new QHBoxLayout();
	pen_section->setContentsMargins(0, 0, 0, 0);
	pen_section->setSpacing(6);

	/* Home penalties column */
	QVBoxLayout *home_pen_col = new QVBoxLayout();
	home_pen_col->setContentsMargins(0, 0, 0, 0);
	home_pen_col->setSpacing(2);
	QLabel *home_pen_title = new QLabel("Home Pen", widget);
	home_pen_title->setStyleSheet(kMutedStyle);
	home_pen_title->setAlignment(Qt::AlignCenter);
	home_pen_col->addWidget(home_pen_title);
	g_home_pen_layout = new QVBoxLayout();
	g_home_pen_layout->setContentsMargins(0, 0, 0, 0);
	g_home_pen_layout->setSpacing(1);
	home_pen_col->addLayout(g_home_pen_layout);
	QPushButton *home_pen_add = new QPushButton("+ Penalty", widget);
	home_pen_col->addWidget(home_pen_add);
	pen_section->addLayout(home_pen_col, 1);

	/* Away penalties column */
	QVBoxLayout *away_pen_col = new QVBoxLayout();
	away_pen_col->setContentsMargins(0, 0, 0, 0);
	away_pen_col->setSpacing(2);
	QLabel *away_pen_title = new QLabel("Away Pen", widget);
	away_pen_title->setStyleSheet(kMutedStyle);
	away_pen_title->setAlignment(Qt::AlignCenter);
	away_pen_col->addWidget(away_pen_title);
	g_away_pen_layout = new QVBoxLayout();
	g_away_pen_layout->setContentsMargins(0, 0, 0, 0);
	g_away_pen_layout->setSpacing(1);
	away_pen_col->addLayout(g_away_pen_layout);
	QPushButton *away_pen_add = new QPushButton("+ Penalty", widget);
	away_pen_col->addWidget(away_pen_add);
	pen_section->addLayout(away_pen_col, 1);

	root->addLayout(pen_section);

	add_separator();

	/* Highlights generation button */
	g_highlights_btn = new QPushButton("Generate Period Highlights", widget);
	g_highlights_btn->setVisible(false);
	root->addWidget(g_highlights_btn);

	/* Hidden process queue (used by CLI integration, not shown in dock) */
	g_queue_container = new QWidget();
	g_queue_layout = new QVBoxLayout(g_queue_container);
	g_queue_layout->setContentsMargins(0, 0, 0, 0);
	g_queue_layout->setSpacing(0);
	g_queue_empty_label = new QLabel("", g_queue_container);
	g_queue_layout->addWidget(g_queue_empty_label);
	g_queue_layout->addStretch(1);
	g_queue_container->hide();

	root->addStretch(1);

	/* Connect signals */
	QObject::connect(clock_minus_min, &QPushButton::clicked, []() {
		scoreboard_clock_adjust_minutes(-1);
		scoreboard_write_all_files();
		update_all_labels();
	});
	QObject::connect(clock_minus_sec, &QPushButton::clicked, []() {
		scoreboard_clock_adjust_seconds(-1);
		scoreboard_write_all_files();
		update_all_labels();
	});
	QObject::connect(clock_plus_sec, &QPushButton::clicked, []() {
		scoreboard_clock_adjust_seconds(1);
		scoreboard_write_all_files();
		update_all_labels();
	});
	QObject::connect(clock_plus_min, &QPushButton::clicked, []() {
		scoreboard_clock_adjust_minutes(1);
		scoreboard_write_all_files();
		update_all_labels();
	});
	QObject::connect(g_clock_btn, &QPushButton::clicked, []() {
		if (scoreboard_clock_is_running())
			scoreboard_clock_stop();
		else
			scoreboard_clock_start();
		update_all_labels();
	});
	QObject::connect(period_adv_btn, &QPushButton::clicked, []() {
		scoreboard_period_advance();
		if (g_auto_highlights)
			start_highlights_generation();
		update_all_labels();
	});
	QObject::connect(period_rew_btn, &QPushButton::clicked, []() {
		scoreboard_period_rewind();
		update_all_labels();
	});
	QObject::connect(home_goal_plus, &QPushButton::clicked, []() {
		scoreboard_increment_home_score();
		fire_cli_event(SB_EVENT_HOME_GOAL);
		update_all_labels();
	});
	QObject::connect(home_goal_minus, &QPushButton::clicked, []() {
		scoreboard_decrement_home_score();
		update_all_labels();
	});
	QObject::connect(away_goal_plus, &QPushButton::clicked, []() {
		scoreboard_increment_away_score();
		fire_cli_event(SB_EVENT_AWAY_GOAL);
		update_all_labels();
	});
	QObject::connect(away_goal_minus, &QPushButton::clicked, []() {
		scoreboard_decrement_away_score();
		update_all_labels();
	});
	QObject::connect(home_shot_plus, &QPushButton::clicked, []() {
		scoreboard_increment_home_shots();
		update_all_labels();
	});
	QObject::connect(home_shot_minus, &QPushButton::clicked, []() {
		scoreboard_decrement_home_shots();
		update_all_labels();
	});
	QObject::connect(away_shot_plus, &QPushButton::clicked, []() {
		scoreboard_increment_away_shots();
		update_all_labels();
	});
	QObject::connect(away_shot_minus, &QPushButton::clicked, []() {
		scoreboard_decrement_away_shots();
		update_all_labels();
	});
	QObject::connect(g_home_name_edit, &QLineEdit::editingFinished, []() {
		scoreboard_set_home_name(
			g_home_name_edit->text().trimmed().toUtf8().constData());
		scoreboard_write_all_files();
	});
	QObject::connect(g_away_name_edit, &QLineEdit::editingFinished, []() {
		scoreboard_set_away_name(
			g_away_name_edit->text().trimmed().toUtf8().constData());
		scoreboard_write_all_files();
	});
	QObject::connect(home_pen_add, &QPushButton::clicked, [widget]() {
		open_add_penalty_dialog(widget, true);
	});
	QObject::connect(away_pen_add, &QPushButton::clicked, [widget]() {
		open_add_penalty_dialog(widget, false);
	});
	QObject::connect(g_highlights_btn, &QPushButton::clicked,
			 []() { start_highlights_generation(); });
	/* Penalty clear buttons are connected dynamically in update_all_labels */

	/* Menu actions */
	QObject::connect(configure_action, &QAction::triggered,
			 [widget]() { open_configure_dialog(widget); });
	QObject::connect(clock_settings_action, &QAction::triggered,
			 [widget]() { open_clock_settings_dialog(widget); });
	QObject::connect(new_game_action, &QAction::triggered, []() {
		scoreboard_new_game();
		update_all_labels();
		fire_cli_event(SB_EVENT_GAME_START);
	});
	QObject::connect(refresh_action, &QAction::triggered, []() {
		scoreboard_read_all_files();
		update_all_labels();
	});
	QObject::connect(about_action, &QAction::triggered,
			 [widget]() { open_about_dialog(widget); });

	/* Timer */
	g_tick_timer = new QTimer(widget);
	g_tick_timer->setInterval(100);
	QObject::connect(g_tick_timer, &QTimer::timeout, on_tick);
	g_tick_timer->start();

	/* Register dock */
	g_dock_widget = widget;
	if (!obs_frontend_add_dock_by_id(kDockId, kDockTitle, g_dock_widget)) {
		delete g_dock_widget;
		g_dock_widget = nullptr;
		g_clock_label = nullptr;
		g_period_label = nullptr;
		g_log_fn = nullptr;
		return false;
	}

	/* Hotkeys */
	register_hotkeys();

	obs_frontend_add_event_callback(on_frontend_event, nullptr);
	load_cli_config();
	update_all_labels();
	update_highlights_button_visibility();
	log_info("[streamn-obs-scoreboard] dock initialized");
	return true;
}

void scoreboard_dock_shutdown(void)
{
	/* Do NOT call unregister_hotkeys() here — OBS saves hotkey bindings
	   during shutdown and cleans up hotkeys after module unload.
	   Unregistering early destroys bindings before OBS persists them. */
	obs_frontend_remove_event_callback(on_frontend_event, nullptr);
	obs_frontend_remove_dock(kDockId);

	if (g_tick_timer) {
		g_tick_timer->stop();
		g_tick_timer = nullptr;
	}

	if (g_highlights_process) {
		g_highlights_process->kill();
		g_highlights_process->deleteLater();
		g_highlights_process = nullptr;
	}
	g_highlights_btn = nullptr;

	for (process_job *job : g_jobs) {
		if (!job)
			continue;
		if (job->process && job->running)
			job->process->kill();
		if (job->process)
			job->process->deleteLater();
		delete job;
	}
	g_jobs.clear();

	g_dock_widget = nullptr;
	g_clock_label = nullptr;
	g_clock_btn = nullptr;
	g_period_label = nullptr;
	g_home_name_edit = nullptr;
	g_away_name_edit = nullptr;
	g_home_score_label = nullptr;
	g_away_score_label = nullptr;
	g_home_shots_label = nullptr;
	g_away_shots_label = nullptr;
	for (penalty_row_widgets *pw : g_home_pen_rows)
		delete pw;
	g_home_pen_rows.clear();
	for (penalty_row_widgets *pw : g_away_pen_rows)
		delete pw;
	g_away_pen_rows.clear();
	g_home_pen_layout = nullptr;
	g_away_pen_layout = nullptr;
	if (g_queue_container) {
		delete g_queue_container;
		g_queue_container = nullptr;
	}
	g_queue_layout = nullptr;
	g_queue_empty_label = nullptr;
	g_log_fn = nullptr;
}
