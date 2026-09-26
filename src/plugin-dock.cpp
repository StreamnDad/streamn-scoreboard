#include "scoreboard-dock.h"
#include "plugin-version.h"
#include "../data/streamn-dad-logo.h"

#include <obs-frontend-api.h>
#include <util/config-file.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include <QtCore/QByteArray>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QProcess>
#include <QtCore/QProcessEnvironment>
#include <QtCore/QPointer>
#include <QtCore/QStringList>
#include <QtCore/QTextStream>
#include <QtCore/QElapsedTimer>
#include <QtCore/QFileSystemWatcher>
#include <QtCore/QTimer>
#include <QtCore/QUrl>
#include <QtGui/QClipboard>
#include <QtGui/QColor>
#include <QtGui/QDesktopServices>
#include <QtGui/QGuiApplication>
#include <QtGui/QPixmap>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QtGui/QAction>
#else
#include <QtWidgets/QAction>
#endif
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QColorDialog>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDialog>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QMenu>
#include <QtWidgets/QMessageBox>
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
const char *kCliExtraArgsKey = "cli_extra_args";
const char *kEnvFileKey = "environment_file";
const char *kRecordChaptersKey = "record_chapters";
const char *kGameClockEnabledKey = "game_clock_enabled";
const char *kGameClockFormatKey = "game_clock_format";
const char *kPenaltyLabelFormatKey = "penalty_label_format";
const char *kScoreColorKey = "score_color";
const char *kFoulsColorKey = "fouls_color";
const char *kClockPadMinutesKey = "clock_pad_minutes";

struct process_job {
	int id = 0;
	QString title;
	QWidget *row = nullptr;
	QLabel *text = nullptr;
	QProgressBar *spinner = nullptr;
	QPushButton *view_logs = nullptr;
	QPushButton *copy_logs = nullptr;
	QPushButton *cancel = nullptr;
	/* QPointer: the process is parented to the dock widget, which OBS may
	   destroy before the module unloads */
	QPointer<QProcess> process;
	QString stdout_log;
	QString stderr_log;
	bool running = false;
	bool completed = false;
	bool cancelled = false;
};

struct penalty_row_widgets {
	QWidget *container = nullptr;
	QLabel *label = nullptr;
	QPushButton *edit_btn = nullptr;
	QPushButton *clear_btn = nullptr;
	int slot = -1;
	bool home = true;
};

/* Global state */
QWidget *g_dock_widget = nullptr;
QLabel *g_clock_label = nullptr;
QLabel *g_game_clock_label = nullptr;
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
QWidget *g_shots_row_widget = nullptr;
QWidget *g_faceoffs_row_widget = nullptr;
QLabel *g_home_faceoffs_label = nullptr;
QLabel *g_away_faceoffs_label = nullptr;
QWidget *g_fouls_row_widget = nullptr;
QLabel *g_home_fouls_label = nullptr;
QLabel *g_away_fouls_label = nullptr;
QLabel *g_fouls_center_label = nullptr;
QWidget *g_fouls2_row_widget = nullptr;
QLabel *g_home_fouls2_label = nullptr;
QLabel *g_away_fouls2_label = nullptr;
QLabel *g_fouls2_center_label = nullptr;
QWidget *g_penalty_section_widget = nullptr;
QFrame *g_penalty_separator = nullptr;
QVBoxLayout *g_queue_layout = nullptr;
QWidget *g_queue_container = nullptr;
QLabel *g_queue_empty_label = nullptr;
QLabel *g_queue_title = nullptr;
QFrame *g_queue_separator = nullptr;
QScrollArea *g_queue_scroll = nullptr;
QPushButton *g_clock_btn = nullptr;
/* Guarded pointers: both objects are children of the dock widget. On OBS
   shutdown the dock (and its children) can be destroyed before
   obs_module_unload() runs, so raw pointers would dangle and calling
   stop() on a deleted QTimer crashes OBS on quit (seen on macOS, OBS 32).
   QPointer resets itself to null when the object is deleted. */
QPointer<QTimer> g_tick_timer;
QPointer<QFileSystemWatcher> g_file_watcher;
QElapsedTimer g_write_cooldown;
QElapsedTimer g_clock_elapsed;
qint64 g_clock_remainder_ms = 0;
scoreboard_log_fn g_log_fn = nullptr;

QVector<process_job *> g_jobs;
int g_next_job_id = 1;
QString g_environment_file;
QPushButton *g_highlights_btn = nullptr;
QPushButton *g_period_adv_btn = nullptr;
QCheckBox *g_game_finished = nullptr;

/* Stream-relative event timestamps */
QElapsedTimer g_stream_timer;
bool g_stream_active = false;
int g_period_start_logged = -1; /* period number for which we already logged a start */
QPushButton *g_copy_timestamps_btn = nullptr;

/* Recording chapter markers (OBS 32+, MP4/MOV only for embedded chapters) */
typedef bool (*add_chapter_fn)(const char *);
typedef char *(*get_last_recording_fn)(void);
add_chapter_fn g_add_chapter = nullptr;
get_last_recording_fn g_get_last_recording = nullptr;
bool g_chapters_api_available = false;
bool g_record_chapters_enabled = false;

/* Dock value colors ("#rrggbb"); empty = theme default */
QString g_score_color;
QString g_fouls_color;
bool g_recording_active = false;
QElapsedTimer g_recording_timer;

struct recording_chapter {
	int offset_seconds;
	QString label;
};
QVector<recording_chapter> g_recording_chapters;

static const int kNumHotkeys = 45;

static const char *kHotkeyNames[kNumHotkeys] = {
	"sb_clock_startstop",  "sb_clock_reset",
	"sb_clock_plus1min",   "sb_clock_minus1min",
	"sb_clock_plus1sec",   "sb_clock_minus1sec",
	"sb_home_goal_plus",   "sb_home_goal_minus",
	"sb_home_shot_plus",   "sb_home_shot_minus",
	"sb_away_goal_plus",   "sb_away_goal_minus",
	"sb_away_shot_plus",   "sb_away_shot_minus",
	"sb_period_advance",   "sb_period_rewind",
	"sb_home_pen_add",     "sb_home_pen_clear1",
	"sb_home_pen_clear2",  "sb_away_pen_add",
	"sb_away_pen_clear1",  "sb_away_pen_clear2",
	"sb_generate_highlights",
	"sb_home_foul_plus",   "sb_home_foul_minus",
	"sb_away_foul_plus",   "sb_away_foul_minus",
	"sb_home_foul2_plus",  "sb_home_foul2_minus",
	"sb_away_foul2_plus",  "sb_away_foul2_minus",
	"sb_home_major_pen_add", "sb_away_major_pen_add",
	"sb_home_fo_plus",     "sb_home_fo_minus",
	"sb_away_fo_plus",     "sb_away_fo_minus",
	"sb_home_2plus2_pen_add", "sb_away_2plus2_pen_add",
	"sb_home_2plus5_pen_add", "sb_away_2plus5_pen_add",
	"sb_home_pen_edit1",   "sb_home_pen_edit2",
	"sb_away_pen_edit1",   "sb_away_pen_edit2",
};

obs_hotkey_id g_hotkey_ids[kNumHotkeys];

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

/* ---- Safety helpers ---- */

bool clock_at_segment_boundary()
{
	int tenths = scoreboard_clock_get_tenths();
	int full = scoreboard_get_period_length() * 10;
	return tenths == 0 || tenths == full;
}

bool confirm_mid_period_action(QWidget *parent, const QString &action)
{
	if (clock_at_segment_boundary())
		return true;
	QDialog dialog(parent);
	dialog.setWindowTitle("Confirm");
	QVBoxLayout *layout = new QVBoxLayout(&dialog);
	char buf[64];
	scoreboard_clock_format(buf, sizeof(buf));
	layout->addWidget(new QLabel(
		"The clock is at " + QString::fromUtf8(buf) +
			". Are you sure you want to " + action + "?",
		&dialog));
	QDialogButtonBox *buttons = new QDialogButtonBox(
		QDialogButtonBox::Yes | QDialogButtonBox::No, &dialog);
	QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog,
			 &QDialog::accept);
	QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog,
			 &QDialog::reject);
	layout->addWidget(buttons);
	return dialog.exec() == QDialog::Accepted;
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
	/* Hide entire queue section when no jobs exist */
	const bool show_section = !g_jobs.isEmpty();
	if (g_queue_separator)
		g_queue_separator->setVisible(show_section);
	if (g_queue_title)
		g_queue_title->setVisible(show_section);
	if (g_queue_scroll)
		g_queue_scroll->setVisible(show_section);
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
			/* Only a failed start is terminal here; for crashes,
			   kills and I/O errors finished() follows and reports
			   the final status. */
			if (error != QProcess::FailedToStart)
				return;
			capture_remaining_process_output(job);
			complete_job(job,
				     "failed to start (check CLI executable)");
		});
	QObject::connect(
		job->process,
		qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
		[job](int exit_code, QProcess::ExitStatus status) {
			capture_remaining_process_output(job);
			if (job->cancelled) {
				complete_job(job, "cancelled");
			} else if (status == QProcess::NormalExit &&
				   exit_code == 0) {
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
			job->cancelled = true;
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


/* Detach a job's process before the job struct is deleted: its signal
   handlers capture the raw job pointer, so any signal emitted afterwards
   (e.g. finished() while the process is torn down) would touch freed
   memory. */
void release_job_process(process_job *job)
{
	if (!job || !job->process)
		return;
	QObject::disconnect(job->process, nullptr, nullptr, nullptr);
	if (job->running)
		job->process->kill();
	job->process->deleteLater();
	job->process = nullptr;
}

void clear_completed_jobs()
{
	QVector<process_job *> remaining;
	for (process_job *job : g_jobs) {
		if (!job)
			continue;
		if (job->completed) {
			if (job->row) {
				g_queue_layout->removeWidget(job->row);
				job->row->hide();
				job->row->deleteLater();
			}
			release_job_process(job);
			delete job;
		} else {
			remaining.push_back(job);
		}
	}
	g_jobs = remaining;
	refresh_queue_placeholder();
}

/* ---- Event timestamp helpers ---- */

void write_timestamps_file();
void update_copy_timestamps_visibility();

/* ---- Recording chapter helpers ---- */

void add_recording_chapter(const char *label)
{
	if (!g_record_chapters_enabled)
		return;
	/* Embed chapter in MP4 via OBS API (if available and recording) */
	if (g_add_chapter)
		g_add_chapter(label);
	/* Track for companion .chapters.txt (works for any format) */
	if (g_recording_active) {
		int offset = (int)(g_recording_timer.elapsed() / 1000);
		g_recording_chapters.append({offset, QString::fromUtf8(label)});
	}
}

void add_recording_chapter_delayed(const char *label, int delay_seconds)
{
	if (!g_record_chapters_enabled)
		return;
	/* MP4 chapter is placed at current PTS (delay cannot be applied) */
	if (g_add_chapter)
		g_add_chapter(label);
	/* Companion file uses the adjusted offset */
	if (g_recording_active) {
		int offset = (int)(g_recording_timer.elapsed() / 1000) -
			     delay_seconds;
		if (offset < 0)
			offset = 0;
		g_recording_chapters.append({offset, QString::fromUtf8(label)});
	}
}

void write_recording_chapters_file(const char *recording_path)
{
	if (g_recording_chapters.isEmpty())
		return;
	QString chapters_path =
		QString::fromUtf8(recording_path) + ".chapters.txt";
	QFile file(chapters_path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
		return;
	QTextStream out(&file);
	for (const recording_chapter &ch : g_recording_chapters) {
		int hours = ch.offset_seconds / 3600;
		int minutes = (ch.offset_seconds % 3600) / 60;
		int seconds = ch.offset_seconds % 60;
		out << QString::asprintf("%d:%02d:%02d %s\n", hours, minutes,
					seconds,
					ch.label.toUtf8().constData());
	}
	log_info("[streamn-obs-scoreboard] wrote " +
		chapters_path);
}

int stream_offset_seconds()
{
	if (!g_stream_active)
		return -1;
	return (int)(g_stream_timer.elapsed() / 1000);
}

/* Default seconds to subtract from goal timestamps to account for
   the delay between a goal being scored and the operator pressing
   the button.  Clamped so it never goes below 0:00:00. */
static const int kGoalDelaySeconds = 10;

void log_event(const char *label)
{
	int offset;
	if (scoreboard_get_game_clock_enabled()) {
		offset = scoreboard_game_clock_get_tenths() / 10;
	} else {
		offset = stream_offset_seconds();
		if (offset < 0)
			return;
	}
	scoreboard_event_log_add(offset, label);
	write_timestamps_file();
	update_copy_timestamps_visibility();
}

void log_event_with_offset(const char *label, int offset)
{
	if (!scoreboard_get_game_clock_enabled() && !g_stream_active)
		return;
	if (offset < 0)
		offset = 0;
	scoreboard_event_log_add(offset, label);
	write_timestamps_file();
	update_copy_timestamps_visibility();
}

void remove_last_event(const char *prefix)
{
	int idx = scoreboard_event_log_find_last(prefix);
	if (idx >= 0) {
		scoreboard_event_log_remove(idx);
		write_timestamps_file();
		update_copy_timestamps_visibility();
	}
}

void log_period_start_event()
{
	int current = scoreboard_get_period();
	if (current == g_period_start_logged)
		return;
	g_period_start_logged = current;

	char buf[SCOREBOARD_EVENT_LABEL_SIZE];
	char period_buf[64];
	scoreboard_format_period(period_buf, sizeof(period_buf));
	snprintf(buf, sizeof(buf), "%s %s Start",
		 scoreboard_get_segment_name(), period_buf);
	log_event(buf);
	add_recording_chapter(buf);
}

void log_period_end_event()
{
	char buf[SCOREBOARD_EVENT_LABEL_SIZE];
	char period_buf[64];
	scoreboard_format_period(period_buf, sizeof(period_buf));
	snprintf(buf, sizeof(buf), "%s %s End",
		 scoreboard_get_segment_name(), period_buf);
	log_event(buf);
	add_recording_chapter(buf);
}

/* Must be called AFTER scoreboard_increment_*_score() so the label
   reflects the score after the goal (e.g. "Goal: Eagles (2-1)").
   Compare with log_period_end_event() which is called BEFORE
   scoreboard_period_advance() to capture the current period number. */
void log_goal_event(bool home)
{
	if (!scoreboard_get_log_scores())
		return;

	const char *label = scoreboard_get_score_label();
	char buf[SCOREBOARD_EVENT_LABEL_SIZE];
	snprintf(buf, sizeof(buf), "%s: %s (%d-%d)", label,
		 home ? scoreboard_get_home_name()
		      : scoreboard_get_away_name(),
		 scoreboard_get_home_score(),
		 scoreboard_get_away_score());

	if (scoreboard_get_game_clock_enabled()) {
		int offset = scoreboard_game_clock_get_tenths() / 10;
		log_event_with_offset(buf, offset - kGoalDelaySeconds);
	} else {
		int offset = stream_offset_seconds();
		if (offset >= 0)
			log_event_with_offset(buf,
					      offset - kGoalDelaySeconds);
	}
	add_recording_chapter_delayed(buf, kGoalDelaySeconds);
}

void remove_goal_event(bool home)
{
	if (!scoreboard_get_log_scores())
		return;
	const char *label = scoreboard_get_score_label();
	char prefix[SCOREBOARD_EVENT_LABEL_SIZE];
	snprintf(prefix, sizeof(prefix), "%s: %s", label,
		 home ? scoreboard_get_home_name()
		      : scoreboard_get_away_name());
	remove_last_event(prefix);
}

void log_penalty_event(bool home, int player_number)
{
	char buf[SCOREBOARD_EVENT_LABEL_SIZE];
	if (player_number > 0)
		snprintf(buf, sizeof(buf), "Penalty: %s #%d",
			 home ? scoreboard_get_home_name()
			      : scoreboard_get_away_name(),
			 player_number);
	else
		snprintf(buf, sizeof(buf), "Penalty: %s",
			 home ? scoreboard_get_home_name()
			      : scoreboard_get_away_name());
	log_event(buf);
	add_recording_chapter(buf);
}

void remove_penalty_event(bool home, int player_number)
{
	char prefix[SCOREBOARD_EVENT_LABEL_SIZE];
	if (player_number > 0)
		snprintf(prefix, sizeof(prefix), "Penalty: %s #%d",
			 home ? scoreboard_get_home_name()
			      : scoreboard_get_away_name(),
			 player_number);
	else
		snprintf(prefix, sizeof(prefix), "Penalty: %s",
			 home ? scoreboard_get_home_name()
			      : scoreboard_get_away_name());
	remove_last_event(prefix);
}

void log_game_end_event()
{
	char buf[SCOREBOARD_EVENT_LABEL_SIZE];
	snprintf(buf, sizeof(buf), "Game End \xe2\x80\x94 %s %d, %s %d",
		 scoreboard_get_home_name(), scoreboard_get_home_score(),
		 scoreboard_get_away_name(), scoreboard_get_away_score());
	log_event(buf);
	add_recording_chapter(buf);
}

bool timestamps_file_path(char *buf, size_t size)
{
	const char *dir = scoreboard_get_output_directory();
	if (dir[0] == '\0')
		return false;
	snprintf(buf, size, "%s/timestamps.txt", dir);
	return true;
}

void write_timestamps_file()
{
	char path[544]; /* 512 (max output dir) + 32 (filename) */
	if (!timestamps_file_path(path, sizeof(path)))
		return;
	scoreboard_event_log_write(path);
}

void update_copy_timestamps_visibility()
{
	if (!g_copy_timestamps_btn)
		return;
	if (scoreboard_event_log_count() > 0) {
		g_copy_timestamps_btn->setVisible(true);
		return;
	}
	char path[544];
	if (timestamps_file_path(path, sizeof(path)) &&
	    scoreboard_event_log_file_has_content(path)) {
		g_copy_timestamps_btn->setVisible(true);
		return;
	}
	g_copy_timestamps_btn->setVisible(false);
}

/* Split user CLI arguments like a shell would, so quoted values with
   spaces ("--title \"Home vs Away\"") stay one argument. */
QStringList split_cli_args(const QString &text)
{
	return QProcess::splitCommand(text);
}

void run_reeln_segment_command()
{
	const QString executable =
		QString::fromUtf8(scoreboard_get_cli_executable()).trimmed();
	if (executable.isEmpty())
		return;
	const QString extra =
		QString::fromUtf8(scoreboard_get_cli_extra_args()).trimmed();
	QStringList extra_parts;
	if (!extra.isEmpty())
		extra_parts = split_cli_args(extra);

	int period = scoreboard_get_period();
	char period_buf[64];
	scoreboard_format_period(period_buf, sizeof(period_buf));

	QStringList args;
	args << "game" << "segment" << QString::number(period) << extra_parts;
	QString title = QString::fromUtf8(scoreboard_get_segment_name()) +
			QString(" ") + QString::fromUtf8(period_buf) +
			QString(" Highlights");
	add_job_row(title, args);
}

void run_reeln_highlights_command()
{
	const QString executable =
		QString::fromUtf8(scoreboard_get_cli_executable()).trimmed();
	if (executable.isEmpty())
		return;
	const QString extra =
		QString::fromUtf8(scoreboard_get_cli_extra_args()).trimmed();
	QStringList extra_parts;
	if (!extra.isEmpty())
		extra_parts = split_cli_args(extra);

	log_game_end_event();
	QStringList args;
	args << "game" << "highlights" << extra_parts;
	add_job_row("Game Highlights", args);
}

void write_files_now();
void open_edit_penalty_dialog(QWidget *parent, bool home, int slot);

/* ---- UI update ---- */

void update_all_labels()
{
	char buf[64];
	if (g_clock_label) {
		scoreboard_clock_format(buf, sizeof(buf));
		g_clock_label->setText(QString::fromUtf8(buf));
	}
	if (g_clock_btn) {
		bool clock_running = scoreboard_clock_is_running();
		g_clock_btn->setText(clock_running ? "Stop" : "Start");
		if (g_highlights_btn)
			g_highlights_btn->setEnabled(!clock_running);
		if (g_period_adv_btn)
			g_period_adv_btn->setEnabled(!clock_running);
		/* setStyleSheet() re-polishes the widget; this runs every
		   100 ms, so only restyle when the running state flips */
		static int s_styled_running = -1;
		if (s_styled_running == (clock_running ? 1 : 0)) {
			/* unchanged */
		} else if (clock_running) {
			s_styled_running = 1;
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
			s_styled_running = 0;
			g_clock_btn->setStyleSheet("");
		}
	}
	if (g_highlights_btn && g_highlights_btn->isVisible()) {
		if (g_game_finished && g_game_finished->isChecked()) {
			g_highlights_btn->setText(
				"Generate Game Highlights");
		} else {
			scoreboard_format_period(buf, sizeof(buf));
			g_highlights_btn->setText(
				"Generate " +
				QString::fromUtf8(
					scoreboard_get_segment_name()) +
				" " + QString::fromUtf8(buf) +
				" Highlights");
		}
	}
	if (g_period_label) {
		scoreboard_format_period(buf, sizeof(buf));
		QString seg = QString::fromUtf8(scoreboard_get_segment_name());
		g_period_label->setText(seg + ": " +
					QString::fromUtf8(buf));
	}
	if (g_game_clock_label) {
		if (scoreboard_get_game_clock_enabled()) {
			char gc_buf[32];
			scoreboard_game_clock_format(gc_buf, sizeof(gc_buf));
			g_game_clock_label->setText(
				"(" + QString::fromUtf8(gc_buf) + ")");
			g_game_clock_label->setVisible(true);
		} else {
			g_game_clock_label->setVisible(false);
		}
	}
	if (g_shots_row_widget)
		g_shots_row_widget->setVisible(scoreboard_get_has_shots());
	if (g_faceoffs_row_widget)
		g_faceoffs_row_widget->setVisible(
			scoreboard_get_has_faceoffs());
	if (g_fouls_row_widget)
		g_fouls_row_widget->setVisible(scoreboard_get_has_fouls());
	if (g_fouls_center_label)
		g_fouls_center_label->setText(
			QString::fromUtf8(scoreboard_get_foul_label()));
	if (g_home_fouls_label)
		g_home_fouls_label->setText(
			QString::number(scoreboard_get_home_fouls()));
	if (g_away_fouls_label)
		g_away_fouls_label->setText(
			QString::number(scoreboard_get_away_fouls()));
	if (g_fouls2_row_widget)
		g_fouls2_row_widget->setVisible(scoreboard_get_has_fouls2());
	if (g_fouls2_center_label)
		g_fouls2_center_label->setText(
			QString::fromUtf8(scoreboard_get_foul_label2()));
	if (g_home_fouls2_label)
		g_home_fouls2_label->setText(
			QString::number(scoreboard_get_home_fouls2()));
	if (g_away_fouls2_label)
		g_away_fouls2_label->setText(
			QString::number(scoreboard_get_away_fouls2()));
	if (g_penalty_section_widget)
		g_penalty_section_widget->setVisible(
			scoreboard_get_has_penalties());
	if (g_penalty_separator)
		g_penalty_separator->setVisible(
			scoreboard_get_has_penalties());
	/* QLineEdit::setText() resets cursor and undo history even when the
	   text is identical, so only update when the name really changed */
	if (g_home_name_edit && !g_home_name_edit->hasFocus()) {
		const QString name =
			QString::fromUtf8(scoreboard_get_home_name());
		if (g_home_name_edit->text() != name)
			g_home_name_edit->setText(name);
	}
	if (g_away_name_edit && !g_away_name_edit->hasFocus()) {
		const QString name =
			QString::fromUtf8(scoreboard_get_away_name());
		if (g_away_name_edit->text() != name)
			g_away_name_edit->setText(name);
	}
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
	if (g_home_faceoffs_label)
		g_home_faceoffs_label->setText(
			QString::number(scoreboard_get_home_faceoffs()));
	if (g_away_faceoffs_label)
		g_away_faceoffs_label->setText(
			QString::number(scoreboard_get_away_faceoffs()));

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
			int idx = 0;
			for (penalty_row_widgets *pw : rows) {
				char nbuf[32], tbuf[32];
				scoreboard_format_penalty_number(
					pw->slot, pw->home, nbuf,
					sizeof(nbuf));
				scoreboard_format_penalty_time(
					pw->slot, pw->home, tbuf,
					sizeof(tbuf));
				QString text = QString::fromUtf8(nbuf)
					+ " " + QString::fromUtf8(tbuf);
				const struct scoreboard_penalty *pen =
					pw->home
					? scoreboard_get_home_penalty(pw->slot)
					: scoreboard_get_away_penalty(pw->slot);
				if (pen && pen->phase2_tenths > 0) {
					int p2s = pen->phase2_tenths / 10;
					char p2buf[16];
					snprintf(p2buf, sizeof(p2buf),
						 " (+%d:%02d)",
						 p2s / 60, p2s % 60);
					text += QString::fromUtf8(p2buf);
				}
				if (idx >= SCOREBOARD_MAX_RUNNING_PENALTIES)
					text += " (queued)";
				pw->label->setText(text);
				idx++;
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

		QWidget *parent_widget = layout->parentWidget();
		int row_idx = 0;
		for (int i : active_slots) {
			char nbuf[32], tbuf[32];
			scoreboard_format_penalty_number(i, home, nbuf,
							 sizeof(nbuf));
			scoreboard_format_penalty_time(i, home, tbuf,
						       sizeof(tbuf));

			penalty_row_widgets *pw = new penalty_row_widgets();
			pw->slot = i;
			pw->home = home;
			pw->container = new QWidget(parent_widget);
			QHBoxLayout *hl = new QHBoxLayout(pw->container);
			hl->setContentsMargins(0, 0, 0, 0);
			hl->setSpacing(4);

			QString text = QString::fromUtf8(nbuf) + " " +
				QString::fromUtf8(tbuf);
			const struct scoreboard_penalty *pen =
				home ? scoreboard_get_home_penalty(i)
				     : scoreboard_get_away_penalty(i);
			if (pen && pen->phase2_tenths > 0) {
				int p2s = pen->phase2_tenths / 10;
				char p2buf[16];
				snprintf(p2buf, sizeof(p2buf),
					 " (+%d:%02d)",
					 p2s / 60, p2s % 60);
				text += QString::fromUtf8(p2buf);
			}
			if (row_idx >= SCOREBOARD_MAX_RUNNING_PENALTIES)
				text += " (queued)";
			pw->label = new QLabel(text);
			pw->label->setStyleSheet("font-size: 11px;");
			row_idx++;

			const char *btn_style =
				"QPushButton { padding: 0px; min-height: 16px; max-height: 18px; font-size: 10px; }";

			pw->edit_btn = new QPushButton("\u270F");
			pw->edit_btn->setFixedSize(18, 18);
			pw->edit_btn->setStyleSheet(btn_style);

			pw->clear_btn = new QPushButton("X");
			pw->clear_btn->setFixedSize(18, 18);
			pw->clear_btn->setStyleSheet(btn_style);

			int captured_slot = i;
			bool captured_home = home;
			QObject::connect(
				pw->edit_btn, &QPushButton::clicked,
				[parent_widget, captured_slot,
				 captured_home]() {
					open_edit_penalty_dialog(
						parent_widget, captured_home,
						captured_slot);
				});
			QObject::connect(
				pw->clear_btn, &QPushButton::clicked,
				[captured_slot, captured_home]() {
					const struct scoreboard_penalty *p =
						captured_home
						? scoreboard_get_home_penalty(captured_slot)
						: scoreboard_get_away_penalty(captured_slot);
					if (!p || !p->active)
						return;
					/* Compound phase 1: transition to
					   phase 2. Otherwise: full clear. */
					if (p->phase2_tenths > 0) {
						if (captured_home)
							scoreboard_home_penalty_set_time(
								captured_slot, 0);
						else
							scoreboard_away_penalty_set_time(
								captured_slot, 0);
					} else {
						remove_penalty_event(
							captured_home,
							p->player_number);
						if (captured_home)
							scoreboard_home_penalty_clear(
								captured_slot);
						else
							scoreboard_away_penalty_clear(
								captured_slot);
						scoreboard_penalty_compact();
					}
					write_files_now();
					update_all_labels();
				});

			hl->addWidget(pw->label, 1);
			hl->addWidget(pw->edit_btn);
			hl->addWidget(pw->clear_btn);
			layout->addWidget(pw->container);
			rows.push_back(pw);
		}
		if (parent_widget)
			parent_widget->adjustSize();
	};

	update_pen_rows(g_home_pen_layout, g_home_pen_rows, true);
	update_pen_rows(g_away_pen_layout, g_away_pen_rows, false);
}

const char *kWatchedFiles[] = {
	"home_name.txt",	  "away_name.txt",
	"home_score.txt",	  "away_score.txt",
	"clock.txt",		  "period.txt",
	"home_shots.txt",	  "away_shots.txt",
	"home_faceoffs.txt",	  "away_faceoffs.txt",
	"home_penalty_numbers.txt", "home_penalty_times.txt",
	"away_penalty_numbers.txt", "away_penalty_times.txt",
	"home_fouls.txt",	    "away_fouls.txt",
	"home_fouls2.txt",	    "away_fouls2.txt",
	"sport.txt",
	"default_penalty_duration.txt",
	"default_major_penalty_duration.txt",
	"period_labels.txt",
	"period_length.txt",
	"cumulative_clock.txt",
	"home_penalty_labels.txt",
	"away_penalty_labels.txt",
};
const int kWatchedFileCount = sizeof(kWatchedFiles) / sizeof(kWatchedFiles[0]);
const qint64 kWriteCooldownMs = 500;

void rebuild_file_watcher()
{
	if (!g_file_watcher)
		return;

	QStringList old_files = g_file_watcher->files();
	if (!old_files.isEmpty())
		g_file_watcher->removePaths(old_files);

	const char *dir = scoreboard_get_output_directory();
	if (dir[0] == '\0')
		return;

	QString base = QString::fromUtf8(dir);
	for (int i = 0; i < kWatchedFileCount; i++) {
		QString path = base + "/" + kWatchedFiles[i];
		if (QFile::exists(path))
			g_file_watcher->addPath(path);
	}
}

/* Adds watched files that exist on disk but are not watched yet */
void watch_missing_files()
{
	if (!g_file_watcher)
		return;
	const char *dir = scoreboard_get_output_directory();
	if (dir[0] == '\0')
		return;
	const QStringList watched = g_file_watcher->files();
	if (watched.size() >= kWatchedFileCount)
		return;
	const QString base = QString::fromUtf8(dir);
	for (int i = 0; i < kWatchedFileCount; i++) {
		const QString path = base + "/" + kWatchedFiles[i];
		if (!watched.contains(path) && QFile::exists(path))
			g_file_watcher->addPath(path);
	}
}

void on_file_changed(const QString &path)
{
	if (g_write_cooldown.isValid() &&
	    g_write_cooldown.elapsed() < kWriteCooldownMs)
		return;

	scoreboard_read_all_files();
	update_all_labels();

	/* Re-add the path — some platforms remove it after a change event */
	if (g_file_watcher && QFile::exists(path) &&
	    !g_file_watcher->files().contains(path))
		g_file_watcher->addPath(path);
}

void write_files_now()
{
	scoreboard_write_all_files();
	g_write_cooldown.restart();
	/* Files that did not exist when the watcher was built (first run,
	   new folder, game clock just enabled) are picked up once they have
	   been written. Checked at most every 5 s to keep ticks cheap. */
	static QElapsedTimer s_last_watch_check;
	if (g_file_watcher && (!s_last_watch_check.isValid() ||
			       s_last_watch_check.elapsed() > 5000)) {
		s_last_watch_check.restart();
		watch_missing_files();
	}
}

void on_tick()
{
	bool was_running = scoreboard_clock_is_running();

	if (was_running) {
		qint64 elapsed_ms = g_clock_elapsed.restart();
		elapsed_ms += g_clock_remainder_ms;
		int elapsed_tenths = (int)(elapsed_ms / 100);
		g_clock_remainder_ms = elapsed_ms % 100;
		if (elapsed_tenths > 0)
			scoreboard_clock_tick(elapsed_tenths);
	} else {
		g_clock_elapsed.restart();
		g_clock_remainder_ms = 0;
	}

	bool is_running = scoreboard_clock_is_running();
	if (scoreboard_is_dirty())
		write_files_now();
	update_all_labels();
	if (was_running && !is_running && g_clock_btn)
		g_clock_btn->repaint();
}

/* ---- Dock value colors ---- */

/* Returns a normalized "#rrggbb" string, or an empty string when the
   input is missing/invalid (meaning "use the theme default"). */
QString sanitize_color(const char *value)
{
	if (value == nullptr || value[0] == '\0')
		return QString();
	QColor c(QString::fromUtf8(value).trimmed());
	return c.isValid() ? c.name() : QString();
}

/* Applies the user-selected colors to the score and foul counters in the
   dock. Score falls back to the theme accent (QPalette::Highlight), fouls
   fall back to the normal text color. Safe to call before the dock
   widgets exist. */
void apply_value_colors()
{
	if (g_home_score_label && g_away_score_label) {
		QString color = g_score_color;
		if (color.isEmpty())
			color = g_home_score_label->palette()
					.color(QPalette::Highlight)
					.name();
		const QString style =
			"font-size: 18px; font-weight: bold; color: " + color +
			";";
		g_home_score_label->setStyleSheet(style);
		g_away_score_label->setStyleSheet(style);
	}
	const QString foul_style =
		g_fouls_color.isEmpty()
			? QString()
			: "font-weight: bold; color: " + g_fouls_color + ";";
	QLabel *foul_labels[] = {g_home_fouls_label, g_away_fouls_label,
				 g_home_fouls2_label, g_away_fouls2_label};
	for (QLabel *label : foul_labels) {
		if (label)
			label->setStyleSheet(foul_style);
	}
}

/* Re-applies the saved "two-digit minutes" choice from the profile.
   Needed after anything that calls scoreboard_set_sport() (e.g. reading
   sport.txt), since that resets the option to the sport's default. When
   the user has never saved a choice, the sport default is kept. */
void apply_saved_clock_pad_minutes()
{
	config_t *profile_cfg = obs_frontend_get_profile_config();
	if (profile_cfg == nullptr ||
	    !config_has_user_value(profile_cfg, kConfigSection,
				   kClockPadMinutesKey))
		return;
	scoreboard_set_clock_pad_minutes(config_get_bool(
		profile_cfg, kConfigSection, kClockPadMinutesKey));
}

/* ---- Profile paths ---- */

void load_profile_paths()
{
	config_t *profile_cfg = obs_frontend_get_profile_config();
	const char *output_dir = nullptr;
	const char *cli_exe = nullptr;
	const char *cli_args = nullptr;
	const char *env_file = nullptr;

	if (profile_cfg != nullptr) {
		output_dir = config_get_string(profile_cfg, kConfigSection,
					       kOutputDirKey);
		cli_exe = config_get_string(profile_cfg, kConfigSection,
					    kCliExecutableKey);
		cli_args = config_get_string(profile_cfg, kConfigSection,
					     kCliExtraArgsKey);
		env_file = config_get_string(profile_cfg, kConfigSection,
					     kEnvFileKey);
		g_record_chapters_enabled = config_get_bool(
			profile_cfg, kConfigSection, kRecordChaptersKey);
		scoreboard_set_game_clock_enabled(config_get_bool(
			profile_cfg, kConfigSection,
			kGameClockEnabledKey));
		scoreboard_set_game_clock_display_format(
			(enum scoreboard_game_clock_format)config_get_int(
				profile_cfg, kConfigSection,
				kGameClockFormatKey));
		const char *pen_fmt = config_get_string(
			profile_cfg, kConfigSection,
			kPenaltyLabelFormatKey);
		if (pen_fmt != nullptr && pen_fmt[0] != '\0')
			scoreboard_set_penalty_label_format(pen_fmt);
		const char *score_color = config_get_string(
			profile_cfg, kConfigSection, kScoreColorKey);
		g_score_color = sanitize_color(score_color);
		const char *fouls_color = config_get_string(
			profile_cfg, kConfigSection, kFoulsColorKey);
		g_fouls_color = sanitize_color(fouls_color);
	}
	apply_saved_clock_pad_minutes();

	scoreboard_set_output_directory(output_dir);
	scoreboard_set_cli_executable(cli_exe);
	scoreboard_set_cli_extra_args(cli_args);
	g_environment_file =
		env_file ? QString::fromUtf8(env_file).trimmed() : QString();
	apply_value_colors();
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
	config_set_string(profile_cfg, kConfigSection, kCliExtraArgsKey,
			  scoreboard_get_cli_extra_args());
	config_set_string(profile_cfg, kConfigSection, kEnvFileKey,
			  g_environment_file.toUtf8().constData());
	config_set_bool(profile_cfg, kConfigSection, kRecordChaptersKey,
			g_record_chapters_enabled);
	config_set_bool(profile_cfg, kConfigSection, kGameClockEnabledKey,
			scoreboard_get_game_clock_enabled());
	config_set_int(profile_cfg, kConfigSection, kGameClockFormatKey,
		       (int)scoreboard_get_game_clock_display_format());
	config_set_string(profile_cfg, kConfigSection,
			  kPenaltyLabelFormatKey,
			  scoreboard_get_penalty_label_format());
	config_set_string(profile_cfg, kConfigSection, kScoreColorKey,
			  g_score_color.toUtf8().constData());
	config_set_string(profile_cfg, kConfigSection, kFoulsColorKey,
			  g_fouls_color.toUtf8().constData());
	config_set_bool(profile_cfg, kConfigSection, kClockPadMinutesKey,
			scoreboard_get_clock_pad_minutes());
	config_save_safe(profile_cfg, "tmp", nullptr);
}

void update_highlights_button_visibility()
{
	const QString cli_path =
		QString::fromUtf8(scoreboard_get_cli_executable()).trimmed();
	const bool show = !cli_path.isEmpty();
	if (g_highlights_btn)
		g_highlights_btn->setVisible(show);
	if (g_game_finished)
		g_game_finished->setVisible(show);
}

/* ---- Dialogs ---- */

void open_add_penalty_dialog(QWidget *parent, bool home,
			     int default_duration_secs = 0,
			     int phase2_secs = 0)
{
	QDialog dialog(parent);
	dialog.setWindowTitle(home ? "Add Home Penalty" : "Add Away Penalty");
	dialog.raise();
	dialog.activateWindow();
	QVBoxLayout *layout = new QVBoxLayout(&dialog);

	QHBoxLayout *num_row = new QHBoxLayout();
	num_row->addWidget(new QLabel("Player #:", &dialog));
	QLineEdit *num_input = new QLineEdit(&dialog);
	num_input->setPlaceholderText("required");
	num_row->addWidget(num_input);
	layout->addLayout(num_row);

	QHBoxLayout *dur_row = new QHBoxLayout();
	dur_row->addWidget(new QLabel("Duration (sec):", &dialog));
	QSpinBox *dur_spin = new QSpinBox(&dialog);
	dur_spin->setRange(1, 1200);
	int dur_val = default_duration_secs > 0
		? default_duration_secs
		: scoreboard_get_default_penalty_duration();
	dur_spin->setValue(dur_val);
	dur_row->addWidget(dur_spin);
	layout->addLayout(dur_row);

	/* Phase 2 toggle buttons — only for sports with major penalties */
	QPushButton *p2_btn_2 = nullptr;
	QPushButton *p2_btn_5 = nullptr;
	QPushButton *p2_btn_10 = nullptr;
	const struct scoreboard_sport_preset *preset =
		scoreboard_get_sport_preset();
	bool show_compound =
		preset && preset->has_penalties &&
		preset->default_major_penalty_secs > 0;

	if (show_compound) {
		QHBoxLayout *p2_row = new QHBoxLayout();
		p2_row->addWidget(new QLabel("Phase 2:", &dialog));
		p2_btn_2 = new QPushButton("+2", &dialog);
		p2_btn_5 = new QPushButton("+5", &dialog);
		p2_btn_10 = new QPushButton("+10", &dialog);
		p2_btn_2->setCheckable(true);
		p2_btn_5->setCheckable(true);
		p2_btn_10->setCheckable(true);

		/* Pre-select based on phase2_secs parameter */
		if (phase2_secs == scoreboard_get_default_penalty_duration())
			p2_btn_2->setChecked(true);
		else if (phase2_secs ==
			 scoreboard_get_default_major_penalty_duration())
			p2_btn_5->setChecked(true);
		else if (phase2_secs == 600)
			p2_btn_10->setChecked(true);

		/* Exclusive + deselectable: clicking a checked button
		   unchecks it; clicking another unchecks the rest */
		auto toggle = [=](QPushButton *clicked) {
			if (clicked->isChecked()) {
				if (clicked != p2_btn_2)
					p2_btn_2->setChecked(false);
				if (clicked != p2_btn_5)
					p2_btn_5->setChecked(false);
				if (clicked != p2_btn_10)
					p2_btn_10->setChecked(false);
			}
		};
		QObject::connect(p2_btn_2, &QPushButton::clicked,
				 [=]() { toggle(p2_btn_2); });
		QObject::connect(p2_btn_5, &QPushButton::clicked,
				 [=]() { toggle(p2_btn_5); });
		QObject::connect(p2_btn_10, &QPushButton::clicked,
				 [=]() { toggle(p2_btn_10); });

		p2_row->addWidget(p2_btn_2);
		p2_row->addWidget(p2_btn_5);
		p2_row->addWidget(p2_btn_10);
		layout->addLayout(p2_row);
	}

	QDialogButtonBox *buttons = new QDialogButtonBox(
		QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
	QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog,
			 &QDialog::accept);
	QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog,
			 &QDialog::reject);
	layout->addWidget(buttons);

	num_input->setFocus();

	if (dialog.exec() == QDialog::Accepted) {
		bool ok = false;
		int player_num = num_input->text().trimmed().toInt(&ok);
		if (!ok)
			player_num = 0;

		/* Determine phase 2 from toggle state */
		int selected_phase2 = 0;
		if (p2_btn_2 && p2_btn_2->isChecked())
			selected_phase2 =
				scoreboard_get_default_penalty_duration();
		else if (p2_btn_5 && p2_btn_5->isChecked())
			selected_phase2 =
				scoreboard_get_default_major_penalty_duration();
		else if (p2_btn_10 && p2_btn_10->isChecked())
			selected_phase2 = 600;

		int slot;
		if (selected_phase2 > 0) {
			if (home)
				slot = scoreboard_home_penalty_add_compound(
					player_num, dur_spin->value(),
					selected_phase2);
			else
				slot = scoreboard_away_penalty_add_compound(
					player_num, dur_spin->value(),
					selected_phase2);
		} else {
			if (home)
				slot = scoreboard_home_penalty_add(
					player_num, dur_spin->value());
			else
				slot = scoreboard_away_penalty_add(
					player_num, dur_spin->value());
		}
		if (slot >= 0)
			log_penalty_event(home, player_num);
		else
			log_info("[streamn-obs-scoreboard] penalty slots full");
		update_all_labels();
	}
}

void open_edit_penalty_dialog(QWidget *parent, bool home, int slot)
{
	const struct scoreboard_penalty *p =
		home ? scoreboard_get_home_penalty(slot)
		     : scoreboard_get_away_penalty(slot);
	if (!p || !p->active)
		return;

	QDialog dialog(parent);
	char title_buf[64];
	snprintf(title_buf, sizeof(title_buf), "Edit %s Penalty #%d",
		 home ? "Home" : "Away", p->player_number);
	dialog.setWindowTitle(QString::fromUtf8(title_buf));
	dialog.raise();
	dialog.activateWindow();
	QVBoxLayout *layout = new QVBoxLayout(&dialog);

	int current_secs = p->remaining_tenths / 10;
	/* The clock keeps running while this modal dialog is open: the
	   penalty may expire and the slots may be compacted, so remember
	   which player it was and find it again on OK. */
	const int player_number = p->player_number;

	QHBoxLayout *dur_row = new QHBoxLayout();
	dur_row->addWidget(new QLabel("Remaining (sec):", &dialog));
	QSpinBox *dur_spin = new QSpinBox(&dialog);
	dur_spin->setRange(0, 1200);
	dur_spin->setValue(current_secs);
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
		int target = -1;
		for (int i = 0; i < SCOREBOARD_MAX_PENALTIES; i++) {
			const struct scoreboard_penalty *cur =
				home ? scoreboard_get_home_penalty(i)
				     : scoreboard_get_away_penalty(i);
			if (cur && cur->active &&
			    cur->player_number == player_number) {
				target = i;
				if (i == slot)
					break; /* prefer the original slot */
			}
		}
		if (target < 0) {
			log_info("[streamn-obs-scoreboard] penalty ended "
				 "while editing — edit discarded");
			update_all_labels();
			return;
		}
		if (home)
			scoreboard_home_penalty_set_time(target,
							 dur_spin->value());
		else
			scoreboard_away_penalty_set_time(target,
							 dur_spin->value());
		write_files_now();
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
		/* Populate the new folder right away (otherwise it stays
		   empty until something changes), then watch the files */
		scoreboard_mark_dirty();
		write_files_now();
		rebuild_file_watcher();
		update_all_labels();
	}
}

void open_clock_settings_dialog(QWidget *parent)
{
	QDialog dialog(parent);
	dialog.setWindowTitle("Game Settings");
	dialog.resize(420, 500);

	QVBoxLayout *dialog_layout = new QVBoxLayout(&dialog);

	QWidget *content_widget = new QWidget(&dialog);
	QVBoxLayout *layout = new QVBoxLayout(content_widget);
	layout->setContentsMargins(0, 0, 0, 0);

	QScrollArea *scroll = new QScrollArea(&dialog);
	scroll->setWidget(content_widget);
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	dialog_layout->addWidget(scroll, 1);

	/* Sport selector */
	QHBoxLayout *sport_row = new QHBoxLayout();
	sport_row->addWidget(new QLabel("Sport:", &dialog));
	QComboBox *sport_combo = new QComboBox(&dialog);
	for (int i = 0; i < SCOREBOARD_SPORT_COUNT; i++) {
		QString name = QString::fromUtf8(
			scoreboard_sport_name((enum scoreboard_sport)i));
		name[0] = name[0].toUpper();
		sport_combo->addItem(name, i);
	}
	sport_combo->setCurrentIndex((int)scoreboard_get_sport());
	sport_row->addWidget(sport_combo, 1);
	layout->addLayout(sport_row);

	QLabel *strength_label =
		new QLabel("Players per side:", &dialog);
	QHBoxLayout *strength_row = new QHBoxLayout();
	strength_row->addWidget(strength_label);
	QSpinBox *strength_spin = new QSpinBox(&dialog);
	strength_spin->setRange(0, 30);
	strength_spin->setValue(scoreboard_get_base_strength());
	strength_row->addWidget(strength_spin);
	layout->addLayout(strength_row);
	bool strength_visible = scoreboard_get_base_strength() > 0;
	strength_label->setVisible(strength_visible);
	strength_spin->setVisible(strength_visible);

	QLabel *len_label = new QLabel("Segment length (minutes):", &dialog);
	QHBoxLayout *len_row = new QHBoxLayout();
	len_row->addWidget(len_label);
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

	QCheckBox *pad_minutes_check = new QCheckBox(
		"Two-digit minutes (09:00 instead of 9:00)", &dialog);
	pad_minutes_check->setChecked(scoreboard_get_clock_pad_minutes());
	pad_minutes_check->setToolTip(
		"Adds a leading zero to the clock when minutes are below 10.\n"
		"Applies to clock.txt and the dock. On by default for futsal.");
	layout->addWidget(pad_minutes_check);

	QLabel *pen_dur_label =
		new QLabel("Minor penalty (seconds):", &dialog);
	QHBoxLayout *pen_dur_row = new QHBoxLayout();
	pen_dur_row->addWidget(pen_dur_label);
	QSpinBox *pen_dur_spin = new QSpinBox(&dialog);
	pen_dur_spin->setRange(1, 600);
	pen_dur_spin->setValue(scoreboard_get_default_penalty_duration());
	pen_dur_row->addWidget(pen_dur_spin);
	layout->addLayout(pen_dur_row);

	QLabel *major_pen_dur_label =
		new QLabel("Major penalty (seconds):", &dialog);
	QHBoxLayout *major_pen_dur_row = new QHBoxLayout();
	major_pen_dur_row->addWidget(major_pen_dur_label);
	QSpinBox *major_pen_dur_spin = new QSpinBox(&dialog);
	major_pen_dur_spin->setRange(1, 1200);
	major_pen_dur_spin->setValue(
		scoreboard_get_default_major_penalty_duration());
	major_pen_dur_row->addWidget(major_pen_dur_spin);
	layout->addLayout(major_pen_dur_row);

	QLabel *pen_label_header =
		new QLabel("<b>Custom Penalty Labels</b>", &dialog);
	layout->addWidget(pen_label_header);

	QHBoxLayout *pen_label_row = new QHBoxLayout();
	QLabel *pen_label_label = new QLabel("Format:", &dialog);
	pen_label_row->addWidget(pen_label_label);
	QLineEdit *pen_label_input = new QLineEdit(&dialog);
	pen_label_input->setText(QString::fromUtf8(
		scoreboard_get_penalty_label_format()));
	pen_label_input->setPlaceholderText(
		"#{{ number }}  {{ time }}{{ if_phase2 }} (+{{ phase2 }}){{ end_if }}");
	pen_label_input->setToolTip(
		"Format for combined penalty label files "
		"(home_penalty_labels.txt, away_penalty_labels.txt).\n"
		"One line per active penalty, empty when no penalties.\n\n"
		"Variables:\n"
		"  {{ number }} \xe2\x80\x94 player number\n"
		"  {{ time }} \xe2\x80\x94 time remaining (M:SS)\n"
		"  {{ phase2 }} \xe2\x80\x94 phase 2 time (compound only)\n\n"
		"Conditional (compound penalties only):\n"
		"  {{ if_phase2 }}...{{ end_if }}\n\n"
		"Example: #{{ number }} {{ time }}"
		"{{ if_phase2 }} (+{{ phase2 }}){{ end_if }}\n"
		"\xe2\x86\x92 Compound: #23 1:57 (+5:00)\n"
		"\xe2\x86\x92 Regular:  #23 1:57");
	pen_label_row->addWidget(pen_label_input, 1);
	layout->addLayout(pen_label_row);

	QLabel *pen_preview_label = new QLabel(&dialog);
	pen_preview_label->setStyleSheet(
		"font-size: 11px; color: gray; padding-left: 4px;");
	pen_preview_label->setWordWrap(true);
	layout->addWidget(pen_preview_label);

	auto update_pen_preview = [pen_preview_label,
				   pen_label_input]() {
		char buf[512];
		scoreboard_preview_penalty_label(
			pen_label_input->text().toUtf8().constData(), buf,
			sizeof(buf));
		QString preview = QString::fromUtf8(buf);
		preview.replace("\n", "\n");
		pen_preview_label->setText("Preview:\n" + preview);
	};
	update_pen_preview();
	QObject::connect(pen_label_input, &QLineEdit::textChanged,
			 [update_pen_preview]() { update_pen_preview(); });

	QLabel *str_label_header =
		new QLabel("<b>Custom Strength Label</b>", &dialog);
	layout->addWidget(str_label_header);

	QHBoxLayout *str_label_row = new QHBoxLayout();
	QLabel *str_label_label = new QLabel("Format:", &dialog);
	str_label_row->addWidget(str_label_label);
	QLineEdit *str_label_input = new QLineEdit(&dialog);
	str_label_input->setText(QString::fromUtf8(
		scoreboard_get_strength_label_format()));
	str_label_input->setPlaceholderText("{{ home }}-{{ away }}");
	str_label_input->setToolTip(
		"Format for strength.txt output.\n\n"
		"Variables:\n"
		"  {{ home }} \xe2\x80\x94 home team strength\n"
		"  {{ away }} \xe2\x80\x94 away team strength\n\n"
		"Conditional (power play only):\n"
		"  {{ if_pp }}...{{ end_if }}\n\n"
		"Examples:\n"
		"  {{ home }}v{{ away }} \xe2\x86\x92 5v4\n"
		"  {{ home }} on {{ away }} \xe2\x86\x92 5 on 4");
	str_label_row->addWidget(str_label_input, 1);
	layout->addLayout(str_label_row);

	QLabel *str_preview_label = new QLabel(&dialog);
	str_preview_label->setStyleSheet(
		"font-size: 11px; color: gray; padding-left: 4px;");
	str_preview_label->setWordWrap(true);
	layout->addWidget(str_preview_label);

	auto update_str_preview = [str_preview_label,
				   str_label_input]() {
		char buf[512];
		scoreboard_preview_strength_label(
			str_label_input->text().toUtf8().constData(), buf,
			sizeof(buf));
		str_preview_label->setText(QString::fromUtf8(buf));
	};
	update_str_preview();
	QObject::connect(str_label_input, &QLineEdit::textChanged,
			 [update_str_preview]() { update_str_preview(); });

	bool str_section_visible = scoreboard_get_base_strength() > 0;
	str_label_header->setVisible(str_section_visible);
	str_label_label->setVisible(str_section_visible);
	str_label_input->setVisible(str_section_visible);
	str_preview_label->setVisible(str_section_visible);


	/* Update dialog fields when sport changes */
	QObject::connect(
		sport_combo, qOverload<int>(&QComboBox::currentIndexChanged),
		[len_spin, down_btn, up_btn, pen_dur_spin, pen_dur_label,
		 major_pen_dur_spin, major_pen_dur_label, pen_label_header,
		 pen_label_label, pen_label_input,
		 pen_preview_label, strength_spin, strength_label,
		 str_label_header, str_label_label, str_label_input,
		 str_preview_label, pad_minutes_check](int index) {
			if (index < 0 || index >= SCOREBOARD_SPORT_COUNT)
				return;
			/* Read defaults straight from the core preset table so
			   the dialog can never drift from the core */
			const struct scoreboard_sport_preset *preset =
				scoreboard_get_preset_for_sport(
					(enum scoreboard_sport)index);
			if (preset->duration_seconds > 0)
				len_spin->setValue(preset->duration_seconds /
						   60);
			if (preset->default_direction ==
			    SCOREBOARD_CLOCK_COUNT_DOWN) {
				down_btn->setChecked(true);
				up_btn->setChecked(false);
			} else {
				up_btn->setChecked(true);
				down_btn->setChecked(false);
			}
			if (preset->default_penalty_secs > 0)
				pen_dur_spin->setValue(
					preset->default_penalty_secs);
			if (preset->default_major_penalty_secs > 0)
				major_pen_dur_spin->setValue(
					preset->default_major_penalty_secs);
			pen_dur_label->setVisible(preset->has_penalties);
			pen_dur_spin->setVisible(preset->has_penalties);
			major_pen_dur_label->setVisible(preset->has_penalties);
			major_pen_dur_spin->setVisible(preset->has_penalties);
			pen_label_header->setVisible(preset->has_penalties);
			pen_label_label->setVisible(preset->has_penalties);
			pen_label_input->setVisible(preset->has_penalties);
			pen_preview_label->setVisible(preset->has_penalties);
			bool show_strength = preset->base_strength > 0;
			strength_label->setVisible(show_strength);
			strength_spin->setVisible(show_strength);
			str_label_header->setVisible(show_strength);
			str_label_label->setVisible(show_strength);
			str_label_input->setVisible(show_strength);
			str_preview_label->setVisible(show_strength);
			if (show_strength)
				strength_spin->setValue(preset->base_strength);
			pad_minutes_check->setChecked(preset->pad_clock_minutes);
		});

	/* Initial visibility of the penalty fields for the current sport
	   (the handler above only runs when the selection changes) */
	{
		const bool has_pen = scoreboard_get_has_penalties();
		pen_dur_label->setVisible(has_pen);
		pen_dur_spin->setVisible(has_pen);
		major_pen_dur_label->setVisible(has_pen);
		major_pen_dur_spin->setVisible(has_pen);
		pen_label_header->setVisible(has_pen);
		pen_label_label->setVisible(has_pen);
		pen_label_input->setVisible(has_pen);
		pen_preview_label->setVisible(has_pen);
	}
	const int initial_len_minutes = len_spin->value();

	/* Period labels button */
	QHBoxLayout *labels_row = new QHBoxLayout();
	labels_row->addWidget(new QLabel("Period labels:", &dialog));
	QPushButton *labels_btn = new QPushButton("Edit...", &dialog);
	labels_row->addWidget(labels_btn, 1);
	layout->addLayout(labels_row);
	QObject::connect(labels_btn, &QPushButton::clicked, [&dialog]() {
		const char *dir = scoreboard_get_output_directory();
		if (dir[0] == '\0') {
			QMessageBox::warning(
				&dialog, "No Output Directory",
				"Set an output directory first.");
			return;
		}
		QString path =
			QString::fromUtf8(dir) + "/period_labels.txt";
		QFile file(path);
		if (!file.exists()) {
			/* Create with current labels so user has a starting point */
			if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
				char buf[512];
				scoreboard_get_period_labels(buf, sizeof(buf));
				file.write(buf);
				file.close();
			}
		}
		QDesktopServices::openUrl(QUrl::fromLocalFile(path));
	});

	QFrame *sep = new QFrame(&dialog);
	sep->setFrameShape(QFrame::HLine);
	sep->setFrameShadow(QFrame::Sunken);
	layout->addWidget(sep);

	QHBoxLayout *cli_row = new QHBoxLayout();
	cli_row->addWidget(new QLabel("reeln-cli path:", &dialog));
	QLineEdit *cli_input = new QLineEdit(&dialog);
	cli_input->setText(
		QString::fromUtf8(scoreboard_get_cli_executable()));
	cli_input->setPlaceholderText("/path/to/reeln");
	QPushButton *cli_browse = new QPushButton("Browse", &dialog);
	cli_row->addWidget(cli_input, 1);
	cli_row->addWidget(cli_browse);
	layout->addLayout(cli_row);

	QLabel *cli_link = new QLabel(
		"<a href=\"https://github.com/StreamnDad/reeln-cli\">github.com/StreamnDad/reeln-cli</a>",
		&dialog);
	cli_link->setOpenExternalLinks(true);
	layout->addWidget(cli_link);

	QObject::connect(cli_browse, &QPushButton::clicked,
			 [&dialog, cli_input]() {
				 const QString path =
					 QFileDialog::getOpenFileName(
						 &dialog,
						 "Select reeln-cli",
						 cli_input->text());
				 if (!path.isEmpty())
					 cli_input->setText(path);
			 });

	QHBoxLayout *cli_args_row = new QHBoxLayout();
	cli_args_row->addWidget(new QLabel("CLI arguments:", &dialog));
	QLineEdit *cli_args_input = new QLineEdit(&dialog);
	cli_args_input->setText(
		QString::fromUtf8(scoreboard_get_cli_extra_args()));
	cli_args_input->setPlaceholderText("--profile my-profile");
	cli_args_row->addWidget(cli_args_input, 1);
	layout->addLayout(cli_args_row);

	QHBoxLayout *env_file_row = new QHBoxLayout();
	env_file_row->addWidget(
		new QLabel("Environment file:", &dialog));
	QLineEdit *env_file_input = new QLineEdit(&dialog);
	env_file_input->setText(g_environment_file);
	env_file_input->setPlaceholderText("/path/to/.env");
	QPushButton *env_file_browse = new QPushButton("Browse", &dialog);
	env_file_row->addWidget(env_file_input, 1);
	env_file_row->addWidget(env_file_browse);
	layout->addLayout(env_file_row);

	QObject::connect(env_file_browse, &QPushButton::clicked,
			 [&dialog, env_file_input]() {
				 const QString path =
					 QFileDialog::getOpenFileName(
						 &dialog,
						 "Select Environment File",
						 env_file_input->text(),
						 "Env Files (*.env);;All Files (*)");
				 if (!path.isEmpty())
					 env_file_input->setText(path);
			 });

	QFrame *sep2 = new QFrame(&dialog);
	sep2->setFrameShape(QFrame::HLine);
	sep2->setFrameShadow(QFrame::Sunken);
	layout->addWidget(sep2);

	QCheckBox *chapters_check =
		new QCheckBox("Record chapters in game file", &dialog);
	chapters_check->setChecked(g_record_chapters_enabled);
	if (g_chapters_api_available) {
		chapters_check->setToolTip(
			"Writes a .chapters.txt file next to each recording "
			"with game event timestamps for reeln-cli.\n\n"
			"Also embeds chapter markers into MP4/MOV recordings "
			"when using Hybrid MP4 output (OBS Settings > Output "
			"> Recording Format > Hybrid MP4).\n"
			"Standard (FFmpeg) and MKV outputs use the companion "
			"file only.");
	} else {
		chapters_check->setToolTip(
			"Writes a .chapters.txt file next to each recording "
			"with game event timestamps for reeln-cli.\n\n"
			"Embedded MP4 chapters require OBS 32+ with Hybrid "
			"MP4 output format.");
	}
	layout->addWidget(chapters_check);

	QLabel *gc_header = new QLabel("<b>Game Clock</b>", &dialog);
	layout->addWidget(gc_header);

	QCheckBox *game_clock_check =
		new QCheckBox("Show cumulative game clock", &dialog);
	game_clock_check->setChecked(scoreboard_get_game_clock_enabled());
	game_clock_check->setToolTip(
		"Tracks total elapsed game time across all periods.\n"
		"Written to cumulative_clock.txt and displayed in the dock.\n"
		"When enabled, event timestamps use game clock time.");
	layout->addWidget(game_clock_check);

	QHBoxLayout *gc_fmt_row = new QHBoxLayout();
	gc_fmt_row->addWidget(new QLabel("Clock format:", &dialog));
	QComboBox *gc_fmt_combo = new QComboBox(&dialog);
	gc_fmt_combo->addItem("MM:SS (e.g. 75:30)");
	gc_fmt_combo->addItem("H:MM:SS (e.g. 1:15:30)");
	gc_fmt_combo->setCurrentIndex(
		(int)scoreboard_get_game_clock_display_format());
	gc_fmt_combo->setEnabled(game_clock_check->isChecked());
	QObject::connect(game_clock_check, &QCheckBox::toggled,
			 gc_fmt_combo, &QComboBox::setEnabled);
	gc_fmt_row->addWidget(gc_fmt_combo, 1);
	layout->addLayout(gc_fmt_row);

	/* Dock colors — lets users pick readable colors for the score and
	   foul counters when the theme accent color has poor contrast. */
	QLabel *colors_header = new QLabel("<b>Dock Colors</b>", &dialog);
	layout->addWidget(colors_header);

	QString pending_score_color = g_score_color;
	QString pending_fouls_color = g_fouls_color;

	auto add_color_row = [&dialog, layout](const QString &label_text,
					       const QString &tooltip,
					       QString *value) {
		QHBoxLayout *row = new QHBoxLayout();
		row->addWidget(new QLabel(label_text, &dialog));
		QPushButton *pick_btn = new QPushButton(&dialog);
		pick_btn->setToolTip(tooltip);
		pick_btn->setMinimumWidth(110);
		QPushButton *reset_btn = new QPushButton("Default", &dialog);
		reset_btn->setToolTip("Use the OBS theme color");
		auto refresh = [pick_btn, reset_btn, value]() {
			if (value->isEmpty()) {
				pick_btn->setText("Theme default");
				pick_btn->setStyleSheet("");
			} else {
				QColor c(*value);
				QString text_color =
					c.lightnessF() > 0.5 ? "#000000"
							     : "#ffffff";
				pick_btn->setText(value->toUpper());
				pick_btn->setStyleSheet(
					"QPushButton { background-color: " +
					*value + "; color: " + text_color +
					"; }");
			}
			reset_btn->setEnabled(!value->isEmpty());
		};
		refresh();
		QObject::connect(pick_btn, &QPushButton::clicked,
				 [&dialog, value, refresh, label_text]() {
					 QColor initial = value->isEmpty()
								  ? QColor(Qt::white)
								  : QColor(*value);
					 QColor c = QColorDialog::getColor(
						 initial, &dialog, label_text);
					 if (c.isValid()) {
						 *value = c.name();
						 refresh();
					 }
				 });
		QObject::connect(reset_btn, &QPushButton::clicked,
				 [value, refresh]() {
					 value->clear();
					 refresh();
				 });
		row->addWidget(pick_btn, 1);
		row->addWidget(reset_btn);
		layout->addLayout(row);
	};
	add_color_row("Score color:",
		      "Color of the score numbers in the dock",
		      &pending_score_color);
	add_color_row("Fouls color:",
		      "Color of the foul / card counters in the dock",
		      &pending_fouls_color);

	QDialogButtonBox *buttons = new QDialogButtonBox(
		QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
	QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog,
			 &QDialog::accept);
	QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog,
			 &QDialog::reject);
	dialog_layout->addWidget(buttons);

	/* Capture pre-dialog clock settings to detect changes */
	int prev_period_length = scoreboard_get_period_length();
	enum scoreboard_clock_direction prev_direction =
		scoreboard_get_clock_direction();

	if (dialog.exec() == QDialog::Accepted) {
		int sport_idx = sport_combo->currentIndex();
		const bool sport_changed =
			sport_idx >= 0 && sport_idx < SCOREBOARD_SPORT_COUNT &&
			sport_idx != (int)scoreboard_get_sport();
		if (sport_changed)
			scoreboard_set_sport(
				(enum scoreboard_sport)sport_idx);
		/* The spin box only holds whole minutes: apply it only when
		   it was changed (by the user or a sport switch), so a
		   non-minute length (e.g. from period_length.txt) is not
		   rounded and the clock is not reset just by pressing OK */
		if (sport_changed ||
		    len_spin->value() != initial_len_minutes)
			scoreboard_set_period_length(len_spin->value() * 60);
		scoreboard_set_clock_pad_minutes(
			pad_minutes_check->isChecked());
		scoreboard_set_clock_direction(
			down_btn->isChecked() ? SCOREBOARD_CLOCK_COUNT_DOWN
					      : SCOREBOARD_CLOCK_COUNT_UP);
		scoreboard_set_default_penalty_duration(
			pen_dur_spin->value());
		scoreboard_set_default_major_penalty_duration(
			major_pen_dur_spin->value());
		scoreboard_set_base_strength(strength_spin->value());
		scoreboard_set_penalty_label_format(
			pen_label_input->text().toUtf8().constData());
		scoreboard_set_strength_label_format(
			str_label_input->text().toUtf8().constData());
		scoreboard_set_cli_executable(
			cli_input->text().trimmed().toUtf8().constData());
		scoreboard_set_cli_extra_args(
			cli_args_input->text().trimmed().toUtf8().constData());
		g_environment_file = env_file_input->text().trimmed();
		g_record_chapters_enabled = chapters_check->isChecked();
		scoreboard_set_game_clock_enabled(
			game_clock_check->isChecked());
		scoreboard_set_game_clock_display_format(
			(enum scoreboard_game_clock_format)
				gc_fmt_combo->currentIndex());
		g_score_color = pending_score_color;
		g_fouls_color = pending_fouls_color;
		apply_value_colors();
		save_profile_paths();
		if (scoreboard_get_period_length() != prev_period_length ||
		    scoreboard_get_clock_direction() != prev_direction)
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
	if (scoreboard_clock_is_running()) {
		scoreboard_clock_stop();
	} else {
		scoreboard_clock_start();
		log_period_start_event();
	}
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
	log_goal_event(true);
}

void hk_home_goal_minus(void *, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (!pressed)
		return;
	remove_goal_event(true);
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
	log_goal_event(false);
}

void hk_away_goal_minus(void *, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (!pressed)
		return;
	remove_goal_event(false);
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

void hk_home_fo_plus(void *, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (pressed)
		scoreboard_increment_home_faceoffs();
}

void hk_home_fo_minus(void *, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (pressed)
		scoreboard_decrement_home_faceoffs();
}

void hk_away_fo_plus(void *, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (pressed)
		scoreboard_increment_away_faceoffs();
}

void hk_away_fo_minus(void *, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (pressed)
		scoreboard_decrement_away_faceoffs();
}

void hk_period_advance(void *, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (!pressed)
		return;
	if (scoreboard_clock_is_running())
		return;
	log_period_end_event();
	scoreboard_period_advance();
}

void hk_period_rewind(void *, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (!pressed)
		return;
	/* Same rule as advance: never while the clock is running */
	if (scoreboard_clock_is_running())
		return;
	scoreboard_period_rewind();
}

void hk_home_pen_add(void *, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (!pressed || !g_dock_widget)
		return;
	QMetaObject::invokeMethod(
		g_dock_widget,
		[=]() { open_add_penalty_dialog(g_dock_widget, true); },
		Qt::QueuedConnection);
}

void hk_home_pen_clear1(void *, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (!pressed)
		return;
	const struct scoreboard_penalty *p = scoreboard_get_home_penalty(0);
	if (!p || !p->active)
		return;
	if (p->phase2_tenths > 0) {
		scoreboard_home_penalty_set_time(0, 0);
	} else {
		remove_penalty_event(true, p->player_number);
		scoreboard_home_penalty_clear(0);
		scoreboard_penalty_compact();
	}
}

void hk_home_pen_clear2(void *, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (!pressed)
		return;
	const struct scoreboard_penalty *p = scoreboard_get_home_penalty(1);
	if (!p || !p->active)
		return;
	if (p->phase2_tenths > 0) {
		scoreboard_home_penalty_set_time(1, 0);
	} else {
		remove_penalty_event(true, p->player_number);
		scoreboard_home_penalty_clear(1);
		scoreboard_penalty_compact();
	}
}

void hk_away_pen_add(void *, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (!pressed || !g_dock_widget)
		return;
	QMetaObject::invokeMethod(
		g_dock_widget,
		[=]() { open_add_penalty_dialog(g_dock_widget, false); },
		Qt::QueuedConnection);
}

void hk_away_pen_clear1(void *, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (!pressed)
		return;
	const struct scoreboard_penalty *p = scoreboard_get_away_penalty(0);
	if (!p || !p->active)
		return;
	if (p->phase2_tenths > 0) {
		scoreboard_away_penalty_set_time(0, 0);
	} else {
		remove_penalty_event(false, p->player_number);
		scoreboard_away_penalty_clear(0);
		scoreboard_penalty_compact();
	}
}

void hk_away_pen_clear2(void *, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (!pressed)
		return;
	const struct scoreboard_penalty *p = scoreboard_get_away_penalty(1);
	if (!p || !p->active)
		return;
	if (p->phase2_tenths > 0) {
		scoreboard_away_penalty_set_time(1, 0);
	} else {
		remove_penalty_event(false, p->player_number);
		scoreboard_away_penalty_clear(1);
		scoreboard_penalty_compact();
	}
}

void hk_home_major_pen_add(void *, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (!pressed || !g_dock_widget)
		return;
	QMetaObject::invokeMethod(
		g_dock_widget,
		[=]() {
			open_add_penalty_dialog(
				g_dock_widget, true,
				scoreboard_get_default_major_penalty_duration());
		},
		Qt::QueuedConnection);
}

void hk_away_major_pen_add(void *, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (!pressed || !g_dock_widget)
		return;
	QMetaObject::invokeMethod(
		g_dock_widget,
		[=]() {
			open_add_penalty_dialog(
				g_dock_widget, false,
				scoreboard_get_default_major_penalty_duration());
		},
		Qt::QueuedConnection);
}

void hk_home_2plus2_pen_add(void *, obs_hotkey_id, obs_hotkey_t *,
			     bool pressed)
{
	if (!pressed || !g_dock_widget)
		return;
	int minor = scoreboard_get_default_penalty_duration();
	QMetaObject::invokeMethod(
		g_dock_widget,
		[=]() {
			open_add_penalty_dialog(g_dock_widget, true, minor,
						minor);
		},
		Qt::QueuedConnection);
}

void hk_away_2plus2_pen_add(void *, obs_hotkey_id, obs_hotkey_t *,
			     bool pressed)
{
	if (!pressed || !g_dock_widget)
		return;
	int minor = scoreboard_get_default_penalty_duration();
	QMetaObject::invokeMethod(
		g_dock_widget,
		[=]() {
			open_add_penalty_dialog(g_dock_widget, false, minor,
						minor);
		},
		Qt::QueuedConnection);
}

void hk_home_2plus5_pen_add(void *, obs_hotkey_id, obs_hotkey_t *,
			     bool pressed)
{
	if (!pressed || !g_dock_widget)
		return;
	int minor = scoreboard_get_default_penalty_duration();
	int major = scoreboard_get_default_major_penalty_duration();
	QMetaObject::invokeMethod(
		g_dock_widget,
		[=]() {
			open_add_penalty_dialog(g_dock_widget, true, minor,
						major);
		},
		Qt::QueuedConnection);
}

void hk_away_2plus5_pen_add(void *, obs_hotkey_id, obs_hotkey_t *,
			     bool pressed)
{
	if (!pressed || !g_dock_widget)
		return;
	int minor = scoreboard_get_default_penalty_duration();
	int major = scoreboard_get_default_major_penalty_duration();
	QMetaObject::invokeMethod(
		g_dock_widget,
		[=]() {
			open_add_penalty_dialog(g_dock_widget, false, minor,
						major);
		},
		Qt::QueuedConnection);
}

void hk_home_pen_edit1(void *, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (!pressed || !g_dock_widget)
		return;
	QMetaObject::invokeMethod(
		g_dock_widget,
		[=]() { open_edit_penalty_dialog(g_dock_widget, true, 0); },
		Qt::QueuedConnection);
}

void hk_home_pen_edit2(void *, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (!pressed || !g_dock_widget)
		return;
	QMetaObject::invokeMethod(
		g_dock_widget,
		[=]() { open_edit_penalty_dialog(g_dock_widget, true, 1); },
		Qt::QueuedConnection);
}

void hk_away_pen_edit1(void *, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (!pressed || !g_dock_widget)
		return;
	QMetaObject::invokeMethod(
		g_dock_widget,
		[=]() { open_edit_penalty_dialog(g_dock_widget, false, 0); },
		Qt::QueuedConnection);
}

void hk_away_pen_edit2(void *, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (!pressed || !g_dock_widget)
		return;
	QMetaObject::invokeMethod(
		g_dock_widget,
		[=]() { open_edit_penalty_dialog(g_dock_widget, false, 1); },
		Qt::QueuedConnection);
}

void hk_generate_highlights(void *, obs_hotkey_id, obs_hotkey_t *,
			     bool pressed)
{
	if (!pressed)
		return;
	if (scoreboard_clock_is_running())
		return;
	if (g_game_finished && g_game_finished->isChecked())
		run_reeln_highlights_command();
	else
		run_reeln_segment_command();
}

void hk_home_foul_plus(void *, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (pressed)
		scoreboard_increment_home_fouls();
}

void hk_home_foul_minus(void *, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (pressed)
		scoreboard_decrement_home_fouls();
}

void hk_away_foul_plus(void *, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (pressed)
		scoreboard_increment_away_fouls();
}

void hk_away_foul_minus(void *, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (pressed)
		scoreboard_decrement_away_fouls();
}

void hk_home_foul2_plus(void *, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (pressed)
		scoreboard_increment_home_fouls2();
}

void hk_home_foul2_minus(void *, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (pressed)
		scoreboard_decrement_home_fouls2();
}

void hk_away_foul2_plus(void *, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (pressed)
		scoreboard_increment_away_fouls2();
}

void hk_away_foul2_minus(void *, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (pressed)
		scoreboard_decrement_away_fouls2();
}

void save_hotkeys(obs_data_t *save_data, bool saving, void *private_data)
{
	(void)save_data;
	(void)private_data;

	config_t *profile_cfg = obs_frontend_get_profile_config();
	if (!profile_cfg)
		return;

	if (saving) {
		obs_data_t *obj = obs_data_create();
		for (int i = 0; i < kNumHotkeys; i++) {
			if (g_hotkey_ids[i] == OBS_INVALID_HOTKEY_ID)
				continue;
			obs_data_array_t *a = obs_hotkey_save(g_hotkey_ids[i]);
			if (a) {
				obs_data_set_array(obj, kHotkeyNames[i], a);
				obs_data_array_release(a);
			}
		}
		const char *json = obs_data_get_json(obj);
		config_set_string(profile_cfg, kConfigSection, "hotkeys", json);
		obs_data_release(obj);
	} else {
		const char *json =
			config_get_string(profile_cfg, kConfigSection, "hotkeys");
		if (!json)
			return;
		obs_data_t *obj = obs_data_create_from_json(json);
		if (!obj)
			return;
		for (int i = 0; i < kNumHotkeys; i++) {
			if (g_hotkey_ids[i] == OBS_INVALID_HOTKEY_ID)
				continue;
			obs_data_array_t *a =
				obs_data_get_array(obj, kHotkeyNames[i]);
			if (a) {
				obs_hotkey_load(g_hotkey_ids[i], a);
				obs_data_array_release(a);
			}
		}
		obs_data_release(obj);
	}
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
	g_hotkey_ids[idx++] = obs_hotkey_register_frontend(
		"sb_home_foul_plus", "Streamn: Home Foul +",
		hk_home_foul_plus, nullptr);
	g_hotkey_ids[idx++] = obs_hotkey_register_frontend(
		"sb_home_foul_minus", "Streamn: Home Foul -",
		hk_home_foul_minus, nullptr);
	g_hotkey_ids[idx++] = obs_hotkey_register_frontend(
		"sb_away_foul_plus", "Streamn: Away Foul +",
		hk_away_foul_plus, nullptr);
	g_hotkey_ids[idx++] = obs_hotkey_register_frontend(
		"sb_away_foul_minus", "Streamn: Away Foul -",
		hk_away_foul_minus, nullptr);
	g_hotkey_ids[idx++] = obs_hotkey_register_frontend(
		"sb_home_foul2_plus", "Streamn: Home Foul2 +",
		hk_home_foul2_plus, nullptr);
	g_hotkey_ids[idx++] = obs_hotkey_register_frontend(
		"sb_home_foul2_minus", "Streamn: Home Foul2 -",
		hk_home_foul2_minus, nullptr);
	g_hotkey_ids[idx++] = obs_hotkey_register_frontend(
		"sb_away_foul2_plus", "Streamn: Away Foul2 +",
		hk_away_foul2_plus, nullptr);
	g_hotkey_ids[idx++] = obs_hotkey_register_frontend(
		"sb_away_foul2_minus", "Streamn: Away Foul2 -",
		hk_away_foul2_minus, nullptr);
	g_hotkey_ids[idx++] = obs_hotkey_register_frontend(
		"sb_home_major_pen_add",
		"Streamn: Home Major Penalty Add",
		hk_home_major_pen_add, nullptr);
	g_hotkey_ids[idx++] = obs_hotkey_register_frontend(
		"sb_away_major_pen_add",
		"Streamn: Away Major Penalty Add",
		hk_away_major_pen_add, nullptr);
	g_hotkey_ids[idx++] = obs_hotkey_register_frontend(
		"sb_home_fo_plus", "Streamn: Home Faceoff +",
		hk_home_fo_plus, nullptr);
	g_hotkey_ids[idx++] = obs_hotkey_register_frontend(
		"sb_home_fo_minus", "Streamn: Home Faceoff -",
		hk_home_fo_minus, nullptr);
	g_hotkey_ids[idx++] = obs_hotkey_register_frontend(
		"sb_away_fo_plus", "Streamn: Away Faceoff +",
		hk_away_fo_plus, nullptr);
	g_hotkey_ids[idx++] = obs_hotkey_register_frontend(
		"sb_away_fo_minus", "Streamn: Away Faceoff -",
		hk_away_fo_minus, nullptr);
	g_hotkey_ids[idx++] = obs_hotkey_register_frontend(
		"sb_home_2plus2_pen_add",
		"Streamn: Home 2+2 Penalty Add",
		hk_home_2plus2_pen_add, nullptr);
	g_hotkey_ids[idx++] = obs_hotkey_register_frontend(
		"sb_away_2plus2_pen_add",
		"Streamn: Away 2+2 Penalty Add",
		hk_away_2plus2_pen_add, nullptr);
	g_hotkey_ids[idx++] = obs_hotkey_register_frontend(
		"sb_home_2plus5_pen_add",
		"Streamn: Home 2+5 Penalty Add",
		hk_home_2plus5_pen_add, nullptr);
	g_hotkey_ids[idx++] = obs_hotkey_register_frontend(
		"sb_away_2plus5_pen_add",
		"Streamn: Away 2+5 Penalty Add",
		hk_away_2plus5_pen_add, nullptr);
	g_hotkey_ids[idx++] = obs_hotkey_register_frontend(
		"sb_home_pen_edit1",
		"Streamn: Home Penalty Edit 1",
		hk_home_pen_edit1, nullptr);
	g_hotkey_ids[idx++] = obs_hotkey_register_frontend(
		"sb_home_pen_edit2",
		"Streamn: Home Penalty Edit 2",
		hk_home_pen_edit2, nullptr);
	g_hotkey_ids[idx++] = obs_hotkey_register_frontend(
		"sb_away_pen_edit1",
		"Streamn: Away Penalty Edit 1",
		hk_away_pen_edit1, nullptr);
	g_hotkey_ids[idx++] = obs_hotkey_register_frontend(
		"sb_away_pen_edit2",
		"Streamn: Away Penalty Edit 2",
		hk_away_pen_edit2, nullptr);

	/* Register save/load callbacks to persist hotkey bindings */
	obs_frontend_add_save_callback(save_hotkeys, nullptr);
}

void on_frontend_event(enum obs_frontend_event event, void *private_data)
{
	(void)private_data;
	if (event == OBS_FRONTEND_EVENT_PROFILE_CHANGED ||
	    event == OBS_FRONTEND_EVENT_FINISHED_LOADING) {
		load_profile_paths();
		rebuild_file_watcher();
		update_all_labels();
		update_highlights_button_visibility();
		update_copy_timestamps_visibility();
	}
	/* OBS is quitting: stop the tick timer now, while the dock still
	   exists, and flush the final state to disk. */
	if (event == OBS_FRONTEND_EVENT_EXIT) {
		if (g_tick_timer)
			g_tick_timer->stop();
		if (scoreboard_is_dirty())
			write_files_now();
		return;
	}
	/* OBS frontend events and Qt button/hotkey callbacks all run on the
	   main (Qt) thread, so g_stream_active and g_stream_timer are safe
	   to access without additional synchronization. */
	if (event == OBS_FRONTEND_EVENT_STREAMING_STARTED) {
		g_stream_timer.start();
		g_stream_active = true;
		g_period_start_logged = -1;

		char ts_path[544];
		bool has_prev = timestamps_file_path(ts_path, sizeof(ts_path)) &&
				scoreboard_event_log_file_has_content(ts_path);

		if (has_prev) {
			/* Defer dialog so we don't block the OBS frontend
			   event callback */
			QString saved_path = QString::fromUtf8(ts_path);
			QTimer::singleShot(0, [saved_path]() {
				QMessageBox box(g_dock_widget);
				box.setWindowTitle("Previous Timestamps");
				box.setText(
					"Timestamps from a previous game exist.\n"
					"Start fresh or keep them?");
				box.setIcon(QMessageBox::Question);
				QPushButton *fresh = box.addButton(
					"Start Fresh",
					QMessageBox::AcceptRole);
				box.addButton("Keep",
					      QMessageBox::RejectRole);
				box.setDefaultButton(fresh);
				box.exec();

				if (box.clickedButton() == fresh) {
					scoreboard_event_log_clear();
					log_event("Stream Start");
				} else {
					scoreboard_event_log_read(
						saved_path.toUtf8()
							.constData());
				}
				update_copy_timestamps_visibility();
			});
		} else {
			scoreboard_event_log_clear();
			log_event("Stream Start");
			update_copy_timestamps_visibility();
		}

		log_info("[streamn-obs-scoreboard] streaming started — "
			 "event timestamps enabled");
	}
	if (event == OBS_FRONTEND_EVENT_STREAMING_STOPPED) {
		g_stream_active = false;
		write_timestamps_file();
		update_copy_timestamps_visibility();
		log_info("[streamn-obs-scoreboard] streaming stopped — "
			 "timestamps written");
	}
	if (event == OBS_FRONTEND_EVENT_RECORDING_STARTED) {
		g_recording_active = true;
		g_recording_timer.start();
		g_recording_chapters.clear();
		add_recording_chapter("Recording Start");
		log_info("[streamn-obs-scoreboard] recording started — "
			 "chapter tracking enabled");
	}
	if (event == OBS_FRONTEND_EVENT_RECORDING_STOPPED) {
		g_recording_active = false;
		if (g_record_chapters_enabled && g_get_last_recording) {
			char *path = g_get_last_recording();
			if (path) {
				write_recording_chapters_file(path);
				bfree(path);
			}
		}
		g_recording_chapters.clear();
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
	apply_saved_clock_pad_minutes();

	/* Detect OBS 32+ recording chapter API at runtime for backwards
	   compatibility.  These symbols only exist in obs-frontend-api 32+. */
#ifdef _WIN32
	HMODULE frontend_mod = GetModuleHandleA("obs-frontend-api");
	if (frontend_mod) {
		g_add_chapter = (add_chapter_fn)GetProcAddress(
			frontend_mod, "obs_frontend_recording_add_chapter");
		g_get_last_recording =
			(get_last_recording_fn)GetProcAddress(
				frontend_mod,
				"obs_frontend_get_last_recording");
	}
#else
	g_add_chapter = (add_chapter_fn)dlsym(
		RTLD_DEFAULT, "obs_frontend_recording_add_chapter");
	g_get_last_recording = (get_last_recording_fn)dlsym(
		RTLD_DEFAULT, "obs_frontend_get_last_recording");
#endif
	g_chapters_api_available = (g_add_chapter != nullptr);
	if (g_chapters_api_available)
		log_info("[streamn-obs-scoreboard] recording chapter API "
			 "available (OBS 32+)");
	else
		log_info("[streamn-obs-scoreboard] recording chapter API "
			 "not found — chapter features disabled");

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
	g_period_label = new QLabel(
		QString::fromUtf8(scoreboard_get_segment_name()) + ": 1",
		widget);
	g_period_label->setAlignment(Qt::AlignCenter);
	QPushButton *period_rew_btn = new QPushButton("<", widget);
	g_period_adv_btn = new QPushButton(">", widget);
	period_rew_btn->setFixedWidth(28);
	g_period_adv_btn->setFixedWidth(28);
	period_row->addWidget(period_rew_btn);
	period_row->addWidget(g_period_label, 1);
	period_row->addWidget(g_period_adv_btn);
	root->addLayout(period_row);

	g_game_clock_label = new QLabel("", widget);
	g_game_clock_label->setAlignment(Qt::AlignCenter);
	g_game_clock_label->setStyleSheet("font-size: 12px; color: gray;");
	g_game_clock_label->setVisible(false);
	root->addWidget(g_game_clock_label);

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

	/* Shots row: [-] 0 [+] | [-] 0 [+] — centered, wrapped for visibility toggle */
	g_shots_row_widget = new QWidget(widget);
	QHBoxLayout *shots_row = new QHBoxLayout(g_shots_row_widget);
	shots_row->setContentsMargins(0, 0, 0, 0);
	shots_row->setSpacing(2);

	QPushButton *home_shot_minus = new QPushButton("-", widget);
	QPushButton *home_shot_plus = new QPushButton("+", widget);
	home_shot_minus->setFixedWidth(28);
	home_shot_plus->setFixedWidth(28);
	home_shot_minus->setToolTip("Home shots on goal -1");
	home_shot_plus->setToolTip("Home shots on goal +1");
	g_home_shots_label = new QLabel("0", widget);
	g_home_shots_label->setAlignment(Qt::AlignCenter);
	g_home_shots_label->setFixedWidth(32);
	g_home_shots_label->setToolTip("Home shots on goal");
	shots_row->addStretch(1);
	shots_row->addWidget(home_shot_minus);
	shots_row->addWidget(g_home_shots_label);
	shots_row->addWidget(home_shot_plus);

	QLabel *shots_label = new QLabel("SOG", widget);
	shots_label->setAlignment(Qt::AlignCenter);
	shots_label->setStyleSheet(kMutedStyle);
	shots_label->setFixedWidth(36);
	shots_label->setToolTip("Shots on Goal");
	shots_row->addSpacing(4);
	shots_row->addWidget(shots_label);
	shots_row->addSpacing(4);

	QPushButton *away_shot_minus = new QPushButton("-", widget);
	QPushButton *away_shot_plus = new QPushButton("+", widget);
	away_shot_minus->setFixedWidth(28);
	away_shot_plus->setFixedWidth(28);
	away_shot_minus->setToolTip("Away shots on goal -1");
	away_shot_plus->setToolTip("Away shots on goal +1");
	g_away_shots_label = new QLabel("0", widget);
	g_away_shots_label->setAlignment(Qt::AlignCenter);
	g_away_shots_label->setFixedWidth(32);
	g_away_shots_label->setToolTip("Away shots on goal");
	shots_row->addWidget(away_shot_minus);
	shots_row->addWidget(g_away_shots_label);
	shots_row->addWidget(away_shot_plus);
	shots_row->addStretch(1);
	root->addWidget(g_shots_row_widget);

	/* Faceoffs row: [-] 0 [+] FO [-] 0 [+] — wrapped for visibility toggle */
	g_faceoffs_row_widget = new QWidget(widget);
	QHBoxLayout *fo_row = new QHBoxLayout(g_faceoffs_row_widget);
	fo_row->setContentsMargins(0, 0, 0, 0);
	fo_row->setSpacing(2);

	QPushButton *home_fo_minus = new QPushButton("-", widget);
	QPushButton *home_fo_plus = new QPushButton("+", widget);
	home_fo_minus->setFixedWidth(28);
	home_fo_plus->setFixedWidth(28);
	home_fo_minus->setToolTip("Home faceoff wins -1");
	home_fo_plus->setToolTip("Home faceoff wins +1");
	g_home_faceoffs_label = new QLabel("0", widget);
	g_home_faceoffs_label->setAlignment(Qt::AlignCenter);
	g_home_faceoffs_label->setFixedWidth(32);
	g_home_faceoffs_label->setToolTip("Home faceoff wins");
	fo_row->addStretch(1);
	fo_row->addWidget(home_fo_minus);
	fo_row->addWidget(g_home_faceoffs_label);
	fo_row->addWidget(home_fo_plus);

	QLabel *fo_label = new QLabel("FO", widget);
	fo_label->setAlignment(Qt::AlignCenter);
	fo_label->setStyleSheet(kMutedStyle);
	fo_label->setFixedWidth(36);
	fo_label->setToolTip("Faceoff Wins");
	fo_row->addSpacing(4);
	fo_row->addWidget(fo_label);
	fo_row->addSpacing(4);

	QPushButton *away_fo_minus = new QPushButton("-", widget);
	QPushButton *away_fo_plus = new QPushButton("+", widget);
	away_fo_minus->setFixedWidth(28);
	away_fo_plus->setFixedWidth(28);
	away_fo_minus->setToolTip("Away faceoff wins -1");
	away_fo_plus->setToolTip("Away faceoff wins +1");
	g_away_faceoffs_label = new QLabel("0", widget);
	g_away_faceoffs_label->setAlignment(Qt::AlignCenter);
	g_away_faceoffs_label->setFixedWidth(32);
	g_away_faceoffs_label->setToolTip("Away faceoff wins");
	fo_row->addWidget(away_fo_minus);
	fo_row->addWidget(g_away_faceoffs_label);
	fo_row->addWidget(away_fo_plus);
	fo_row->addStretch(1);
	root->addWidget(g_faceoffs_row_widget);

	/* Fouls row: [-] 0 [+] | Label | [-] 0 [+] — wrapped for visibility toggle */
	g_fouls_row_widget = new QWidget(widget);
	QHBoxLayout *fouls_row = new QHBoxLayout(g_fouls_row_widget);
	fouls_row->setContentsMargins(0, 0, 0, 0);
	fouls_row->setSpacing(2);

	QPushButton *home_foul_minus = new QPushButton("-", widget);
	QPushButton *home_foul_plus = new QPushButton("+", widget);
	home_foul_minus->setFixedWidth(28);
	home_foul_plus->setFixedWidth(28);
	g_home_fouls_label = new QLabel("0", widget);
	g_home_fouls_label->setAlignment(Qt::AlignCenter);
	g_home_fouls_label->setFixedWidth(32);
	fouls_row->addStretch(1);
	fouls_row->addWidget(home_foul_minus);
	fouls_row->addWidget(g_home_fouls_label);
	fouls_row->addWidget(home_foul_plus);

	g_fouls_center_label = new QLabel(
		QString::fromUtf8(scoreboard_get_foul_label()), widget);
	g_fouls_center_label->setAlignment(Qt::AlignCenter);
	g_fouls_center_label->setStyleSheet(kMutedStyle);
	g_fouls_center_label->setFixedWidth(36);
	fouls_row->addSpacing(4);
	fouls_row->addWidget(g_fouls_center_label);
	fouls_row->addSpacing(4);

	QPushButton *away_foul_minus = new QPushButton("-", widget);
	QPushButton *away_foul_plus = new QPushButton("+", widget);
	away_foul_minus->setFixedWidth(28);
	away_foul_plus->setFixedWidth(28);
	g_away_fouls_label = new QLabel("0", widget);
	g_away_fouls_label->setAlignment(Qt::AlignCenter);
	g_away_fouls_label->setFixedWidth(32);
	fouls_row->addWidget(away_foul_minus);
	fouls_row->addWidget(g_away_fouls_label);
	fouls_row->addWidget(away_foul_plus);
	fouls_row->addStretch(1);
	root->addWidget(g_fouls_row_widget);

	/* Fouls2 row: [-] 0 [+] | Label | [-] 0 [+] — wrapped for visibility toggle */
	g_fouls2_row_widget = new QWidget(widget);
	QHBoxLayout *fouls2_row = new QHBoxLayout(g_fouls2_row_widget);
	fouls2_row->setContentsMargins(0, 0, 0, 0);
	fouls2_row->setSpacing(2);

	QPushButton *home_foul2_minus = new QPushButton("-", widget);
	QPushButton *home_foul2_plus = new QPushButton("+", widget);
	home_foul2_minus->setFixedWidth(28);
	home_foul2_plus->setFixedWidth(28);
	g_home_fouls2_label = new QLabel("0", widget);
	g_home_fouls2_label->setAlignment(Qt::AlignCenter);
	g_home_fouls2_label->setFixedWidth(32);
	fouls2_row->addStretch(1);
	fouls2_row->addWidget(home_foul2_minus);
	fouls2_row->addWidget(g_home_fouls2_label);
	fouls2_row->addWidget(home_foul2_plus);

	g_fouls2_center_label = new QLabel(
		QString::fromUtf8(scoreboard_get_foul_label2()), widget);
	g_fouls2_center_label->setAlignment(Qt::AlignCenter);
	g_fouls2_center_label->setStyleSheet(kMutedStyle);
	g_fouls2_center_label->setFixedWidth(36);
	fouls2_row->addSpacing(4);
	fouls2_row->addWidget(g_fouls2_center_label);
	fouls2_row->addSpacing(4);

	QPushButton *away_foul2_minus = new QPushButton("-", widget);
	QPushButton *away_foul2_plus = new QPushButton("+", widget);
	away_foul2_minus->setFixedWidth(28);
	away_foul2_plus->setFixedWidth(28);
	g_away_fouls2_label = new QLabel("0", widget);
	g_away_fouls2_label->setAlignment(Qt::AlignCenter);
	g_away_fouls2_label->setFixedWidth(32);
	fouls2_row->addWidget(away_foul2_minus);
	fouls2_row->addWidget(g_away_fouls2_label);
	fouls2_row->addWidget(away_foul2_plus);
	fouls2_row->addStretch(1);
	root->addWidget(g_fouls2_row_widget);
	apply_value_colors();

	/* Penalty separator — hidden when penalties are off */
	g_penalty_separator = new QFrame(widget);
	g_penalty_separator->setFrameShape(QFrame::HLine);
	g_penalty_separator->setFrameShadow(QFrame::Sunken);
	g_penalty_separator->setFixedHeight(1);
	root->addWidget(g_penalty_separator);

	/* ---- SECTION: Penalties (dynamic, wrapped for visibility toggle) ---- */
	g_penalty_section_widget = new QWidget(widget);
	QVBoxLayout *pen_wrapper = new QVBoxLayout(g_penalty_section_widget);
	pen_wrapper->setContentsMargins(0, 0, 0, 0);
	pen_wrapper->setSpacing(2);

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
	QWidget *home_pen_container = new QWidget(widget);
	g_home_pen_layout = new QVBoxLayout(home_pen_container);
	g_home_pen_layout->setContentsMargins(0, 0, 0, 0);
	g_home_pen_layout->setSpacing(1);
	QScrollArea *home_pen_scroll = new QScrollArea(widget);
	home_pen_scroll->setWidget(home_pen_container);
	home_pen_scroll->setWidgetResizable(true);
	home_pen_scroll->setFrameShape(QFrame::NoFrame);
	home_pen_scroll->setMaximumHeight(66);
	home_pen_scroll->setHorizontalScrollBarPolicy(
		Qt::ScrollBarAlwaysOff);
	home_pen_col->addWidget(home_pen_scroll);
	QHBoxLayout *home_pen_btns = new QHBoxLayout();
	home_pen_btns->setContentsMargins(0, 0, 0, 0);
	home_pen_btns->setSpacing(2);
	QPushButton *home_pen_add = new QPushButton("+ Minor", widget);
	QPushButton *home_major_pen_add =
		new QPushButton("+ Major", widget);
	home_pen_btns->addWidget(home_pen_add, 1);
	home_pen_btns->addWidget(home_major_pen_add, 1);
	{
		QPalette p = home_major_pen_add->palette();
		QColor base = p.color(QPalette::Button);
		QColor tint = QColor::fromHslF(
			0.08, 0.5, qBound(0.0, base.lightnessF(), 1.0));
		home_major_pen_add->setStyleSheet(
			"QPushButton { background-color: " + tint.name() +
			"; }");
	}
	home_pen_col->addLayout(home_pen_btns);
	pen_section->addLayout(home_pen_col, 1);

	/* Away penalties column */
	QVBoxLayout *away_pen_col = new QVBoxLayout();
	away_pen_col->setContentsMargins(0, 0, 0, 0);
	away_pen_col->setSpacing(2);
	QLabel *away_pen_title = new QLabel("Away Pen", widget);
	away_pen_title->setStyleSheet(kMutedStyle);
	away_pen_title->setAlignment(Qt::AlignCenter);
	away_pen_col->addWidget(away_pen_title);
	QWidget *away_pen_container = new QWidget(widget);
	g_away_pen_layout = new QVBoxLayout(away_pen_container);
	g_away_pen_layout->setContentsMargins(0, 0, 0, 0);
	g_away_pen_layout->setSpacing(1);
	QScrollArea *away_pen_scroll = new QScrollArea(widget);
	away_pen_scroll->setWidget(away_pen_container);
	away_pen_scroll->setWidgetResizable(true);
	away_pen_scroll->setFrameShape(QFrame::NoFrame);
	away_pen_scroll->setMaximumHeight(66);
	away_pen_scroll->setHorizontalScrollBarPolicy(
		Qt::ScrollBarAlwaysOff);
	away_pen_col->addWidget(away_pen_scroll);
	QHBoxLayout *away_pen_btns = new QHBoxLayout();
	away_pen_btns->setContentsMargins(0, 0, 0, 0);
	away_pen_btns->setSpacing(2);
	QPushButton *away_pen_add = new QPushButton("+ Minor", widget);
	QPushButton *away_major_pen_add =
		new QPushButton("+ Major", widget);
	away_pen_btns->addWidget(away_pen_add, 1);
	away_pen_btns->addWidget(away_major_pen_add, 1);
	{
		QPalette p = away_major_pen_add->palette();
		QColor base = p.color(QPalette::Button);
		QColor tint = QColor::fromHslF(
			0.08, 0.5, qBound(0.0, base.lightnessF(), 1.0));
		away_major_pen_add->setStyleSheet(
			"QPushButton { background-color: " + tint.name() +
			"; }");
	}
	away_pen_col->addLayout(away_pen_btns);
	pen_section->addLayout(away_pen_col, 1);

	pen_wrapper->addLayout(pen_section);
	root->addWidget(g_penalty_section_widget);

	add_separator();

	/* Highlights generation row: [Generate {Segment} Highlights] [Game Finished] */
	QHBoxLayout *highlights_row = new QHBoxLayout();
	highlights_row->setContentsMargins(0, 0, 0, 0);
	highlights_row->setSpacing(6);
	g_highlights_btn = new QPushButton("Generate Period Highlights", widget);
	g_highlights_btn->setVisible(false);
	g_game_finished = new QCheckBox("Game Finished", widget);
	g_game_finished->setVisible(false);
	highlights_row->addWidget(g_highlights_btn, 1);
	highlights_row->addWidget(g_game_finished);
	root->addLayout(highlights_row);

	/* Copy Timestamps button (visible after events are logged) */
	g_copy_timestamps_btn =
		new QPushButton("Copy Timestamps to Clipboard", widget);
	g_copy_timestamps_btn->setVisible(false);
	root->addWidget(g_copy_timestamps_btn);

	/* ---- SECTION: Process Queue (hidden until a job is added) ---- */
	g_queue_separator = new QFrame(widget);
	g_queue_separator->setFrameShape(QFrame::HLine);
	g_queue_separator->setFrameShadow(QFrame::Sunken);
	g_queue_separator->setFixedHeight(1);
	g_queue_separator->setVisible(false);
	root->addWidget(g_queue_separator);

	g_queue_title = new QLabel("Process Queue", widget);
	g_queue_title->setStyleSheet(kMutedStyle);
	g_queue_title->setAlignment(Qt::AlignCenter);
	g_queue_title->setVisible(false);
	root->addWidget(g_queue_title);

	g_queue_container = new QWidget();
	g_queue_layout = new QVBoxLayout(g_queue_container);
	g_queue_layout->setContentsMargins(0, 0, 0, 0);
	g_queue_layout->setSpacing(0);
	g_queue_empty_label = new QLabel("No CLI jobs", g_queue_container);
	g_queue_empty_label->setAlignment(Qt::AlignCenter);
	g_queue_empty_label->setStyleSheet(kMutedStyle);
	g_queue_layout->addWidget(g_queue_empty_label);

	g_queue_scroll = new QScrollArea(widget);
	g_queue_scroll->setWidgetResizable(true);
	g_queue_scroll->setWidget(g_queue_container);
	g_queue_scroll->setFrameShape(QFrame::NoFrame);
	g_queue_scroll->setVisible(false);
	root->addWidget(g_queue_scroll, 1);

	g_queue_container->setContextMenuPolicy(Qt::CustomContextMenu);
	QObject::connect(
		g_queue_container, &QWidget::customContextMenuRequested,
		[](const QPoint &pos) {
			QMenu menu;
			QAction *clear_action =
				menu.addAction("Clear Completed Jobs");
			QAction *chosen =
				menu.exec(g_queue_container->mapToGlobal(pos));
			if (chosen == clear_action)
				clear_completed_jobs();
		});

	/* Connect signals */
	QObject::connect(clock_minus_min, &QPushButton::clicked, []() {
		scoreboard_clock_adjust_minutes(-1);
		write_files_now();
		update_all_labels();
	});
	QObject::connect(clock_minus_sec, &QPushButton::clicked, []() {
		scoreboard_clock_adjust_seconds(-1);
		write_files_now();
		update_all_labels();
	});
	QObject::connect(clock_plus_sec, &QPushButton::clicked, []() {
		scoreboard_clock_adjust_seconds(1);
		write_files_now();
		update_all_labels();
	});
	QObject::connect(clock_plus_min, &QPushButton::clicked, []() {
		scoreboard_clock_adjust_minutes(1);
		write_files_now();
		update_all_labels();
	});
	QObject::connect(g_clock_btn, &QPushButton::clicked, []() {
		if (scoreboard_clock_is_running()) {
			scoreboard_clock_stop();
		} else {
			scoreboard_clock_start();
			log_period_start_event();
		}
		update_all_labels();
	});
	QObject::connect(g_period_adv_btn, &QPushButton::clicked, []() {
		if (!confirm_mid_period_action(g_dock_widget,
					       "advance the period"))
			return;
		log_period_end_event();
		scoreboard_period_advance();
		update_all_labels();
	});
	QObject::connect(period_rew_btn, &QPushButton::clicked, []() {
		/* Rewinding resets the clock: guard it like advance */
		if (scoreboard_clock_is_running() ||
		    !confirm_mid_period_action(g_dock_widget,
					       "go back to the previous period"))
			return;
		scoreboard_period_rewind();
		update_all_labels();
	});
	QObject::connect(home_goal_plus, &QPushButton::clicked, []() {
		scoreboard_increment_home_score();
		log_goal_event(true);
		update_all_labels();
	});
	QObject::connect(home_goal_minus, &QPushButton::clicked, []() {
		remove_goal_event(true);
		scoreboard_decrement_home_score();
		update_all_labels();
	});
	QObject::connect(away_goal_plus, &QPushButton::clicked, []() {
		scoreboard_increment_away_score();
		log_goal_event(false);
		update_all_labels();
	});
	QObject::connect(away_goal_minus, &QPushButton::clicked, []() {
		remove_goal_event(false);
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
	QObject::connect(home_fo_plus, &QPushButton::clicked, []() {
		scoreboard_increment_home_faceoffs();
		update_all_labels();
	});
	QObject::connect(home_fo_minus, &QPushButton::clicked, []() {
		scoreboard_decrement_home_faceoffs();
		update_all_labels();
	});
	QObject::connect(away_fo_plus, &QPushButton::clicked, []() {
		scoreboard_increment_away_faceoffs();
		update_all_labels();
	});
	QObject::connect(away_fo_minus, &QPushButton::clicked, []() {
		scoreboard_decrement_away_faceoffs();
		update_all_labels();
	});
	QObject::connect(home_foul_plus, &QPushButton::clicked, []() {
		scoreboard_increment_home_fouls();
		write_files_now();
		update_all_labels();
	});
	QObject::connect(home_foul_minus, &QPushButton::clicked, []() {
		scoreboard_decrement_home_fouls();
		write_files_now();
		update_all_labels();
	});
	QObject::connect(away_foul_plus, &QPushButton::clicked, []() {
		scoreboard_increment_away_fouls();
		write_files_now();
		update_all_labels();
	});
	QObject::connect(away_foul_minus, &QPushButton::clicked, []() {
		scoreboard_decrement_away_fouls();
		write_files_now();
		update_all_labels();
	});
	QObject::connect(home_foul2_plus, &QPushButton::clicked, []() {
		scoreboard_increment_home_fouls2();
		write_files_now();
		update_all_labels();
	});
	QObject::connect(home_foul2_minus, &QPushButton::clicked, []() {
		scoreboard_decrement_home_fouls2();
		write_files_now();
		update_all_labels();
	});
	QObject::connect(away_foul2_plus, &QPushButton::clicked, []() {
		scoreboard_increment_away_fouls2();
		write_files_now();
		update_all_labels();
	});
	QObject::connect(away_foul2_minus, &QPushButton::clicked, []() {
		scoreboard_decrement_away_fouls2();
		write_files_now();
		update_all_labels();
	});
	QObject::connect(g_home_name_edit, &QLineEdit::editingFinished, []() {
		scoreboard_set_home_name(
			g_home_name_edit->text().trimmed().toUtf8().constData());
		write_files_now();
	});
	QObject::connect(g_away_name_edit, &QLineEdit::editingFinished, []() {
		scoreboard_set_away_name(
			g_away_name_edit->text().trimmed().toUtf8().constData());
		write_files_now();
	});
	QObject::connect(home_pen_add, &QPushButton::clicked, [widget]() {
		open_add_penalty_dialog(widget, true);
	});
	QObject::connect(away_pen_add, &QPushButton::clicked, [widget]() {
		open_add_penalty_dialog(widget, false);
	});
	QObject::connect(home_major_pen_add, &QPushButton::clicked,
			 [widget]() {
		open_add_penalty_dialog(
			widget, true,
			scoreboard_get_default_major_penalty_duration());
	});
	QObject::connect(away_major_pen_add, &QPushButton::clicked,
			 [widget]() {
		open_add_penalty_dialog(
			widget, false,
			scoreboard_get_default_major_penalty_duration());
	});
	QObject::connect(g_highlights_btn, &QPushButton::clicked, []() {
		if (g_game_finished && g_game_finished->isChecked()) {
			if (confirm_mid_period_action(g_dock_widget,
						      "generate highlights"))
				run_reeln_highlights_command();
		} else {
			if (confirm_mid_period_action(
				    g_dock_widget,
				    "generate segment highlights"))
				run_reeln_segment_command();
		}
	});
	QObject::connect(g_game_finished, &QCheckBox::toggled,
			 []() { update_all_labels(); });
	QObject::connect(g_copy_timestamps_btn, &QPushButton::clicked, []() {
		QString text;
		int count = scoreboard_event_log_count();
		if (count > 0) {
			/* Build from in-memory event log */
			for (int i = 0; i < count; i++) {
				const struct scoreboard_game_event *ev =
					scoreboard_event_log_get(i);
				if (!ev)
					continue;
				int total = ev->offset_seconds;
				int hours = total / 3600;
				int minutes = (total % 3600) / 60;
				int seconds = total % 60;
				if (!text.isEmpty())
					text += "\n";
				text += QString::asprintf(
					"%d:%02d:%02d %s", hours, minutes,
					seconds, ev->label);
			}
		} else {
			/* Fall back to timestamps.txt on disk */
			char path[544];
			if (timestamps_file_path(path, sizeof(path))) {
				QFile file(QString::fromUtf8(path));
				if (file.open(QIODevice::ReadOnly |
					      QIODevice::Text))
					text = QString::fromUtf8(
						       file.readAll())
						       .trimmed();
			}
		}
		if (!text.isEmpty()) {
			QGuiApplication::clipboard()->setText(text);
			log_info("[streamn-obs-scoreboard] timestamps "
				 "copied to clipboard");
		}
	});
	/* Penalty clear buttons are connected dynamically in update_all_labels */

	/* Menu actions */
	QObject::connect(configure_action, &QAction::triggered,
			 [widget]() { open_configure_dialog(widget); });
	QObject::connect(clock_settings_action, &QAction::triggered,
			 [widget]() { open_clock_settings_dialog(widget); });
	QObject::connect(new_game_action, &QAction::triggered, []() {
		scoreboard_new_game();
		g_period_start_logged = -1;
		scoreboard_event_log_clear();
		if (g_game_finished)
			g_game_finished->setChecked(false);
		write_timestamps_file();
		update_copy_timestamps_visibility();
		update_all_labels();
	});
	QObject::connect(refresh_action, &QAction::triggered, []() {
		scoreboard_read_all_files();
		update_all_labels();
	});
	QObject::connect(about_action, &QAction::triggered,
			 [widget]() { open_about_dialog(widget); });

	/* Timer — uses wall-clock elapsed time for accurate ticking */
	g_tick_timer = new QTimer(widget);
	g_tick_timer->setInterval(100);
	g_clock_elapsed.start();
	QObject::connect(g_tick_timer, &QTimer::timeout, on_tick);
	g_tick_timer->start();

	/* File watcher for external changes */
	g_file_watcher = new QFileSystemWatcher(widget);
	QObject::connect(g_file_watcher, &QFileSystemWatcher::fileChanged,
			 on_file_changed);
	rebuild_file_watcher();

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
	update_all_labels();
	update_highlights_button_visibility();
	log_info("[streamn-obs-scoreboard] dock initialized");
	return true;
}

void scoreboard_dock_shutdown(void)
{
	/* Remove save callback before shutdown */
	obs_frontend_remove_save_callback(save_hotkeys, nullptr);

	obs_frontend_remove_event_callback(on_frontend_event, nullptr);

	/* Stop ticking before the dock goes away. If OBS already destroyed
	   the dock, the QPointer is null and this is skipped. */
	if (g_tick_timer)
		g_tick_timer->stop();
	g_tick_timer = nullptr;

	obs_frontend_remove_dock(kDockId);

	g_file_watcher = nullptr;

	g_highlights_btn = nullptr;
	g_period_adv_btn = nullptr;
	g_game_finished = nullptr;
	g_copy_timestamps_btn = nullptr;
	g_stream_active = false;
	g_period_start_logged = -1;
	g_recording_active = false;
	g_recording_chapters.clear();

	for (process_job *job : g_jobs) {
		if (!job)
			continue;
		release_job_process(job);
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
	g_faceoffs_row_widget = nullptr;
	g_home_faceoffs_label = nullptr;
	g_away_faceoffs_label = nullptr;
	g_fouls_row_widget = nullptr;
	g_home_fouls_label = nullptr;
	g_away_fouls_label = nullptr;
	g_fouls_center_label = nullptr;
	g_fouls2_row_widget = nullptr;
	g_home_fouls2_label = nullptr;
	g_away_fouls2_label = nullptr;
	g_fouls2_center_label = nullptr;
	for (penalty_row_widgets *pw : g_home_pen_rows)
		delete pw;
	g_home_pen_rows.clear();
	for (penalty_row_widgets *pw : g_away_pen_rows)
		delete pw;
	g_away_pen_rows.clear();
	g_home_pen_layout = nullptr;
	g_away_pen_layout = nullptr;
	/* g_queue_container is owned by the QScrollArea in the widget tree;
	   it gets deleted when OBS removes the dock widget. */
	g_queue_container = nullptr;
	g_queue_layout = nullptr;
	g_queue_empty_label = nullptr;
	g_queue_title = nullptr;
	g_queue_separator = nullptr;
	g_queue_scroll = nullptr;
	g_log_fn = nullptr;
}
