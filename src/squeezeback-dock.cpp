#include <QWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTimer>
#include <QPainter>
#include <QLabel>
#include <QDoubleSpinBox>
#include <obs-module.h>
#include <obs-frontend-api.h>
#include "squeezeback-dock-bridge.h"

/* ──────────────────────────────────────────────
 * Progress Ring Widget
 * ────────────────────────────────────────────── */

class ProgressRing : public QWidget {
public:
	ProgressRing(QWidget *parent = nullptr) : QWidget(parent)
	{
		setFixedSize(24, 24);
	}

	void setState(const squeezeback_dock_state &s)
	{
		state = s;
		update();
	}

protected:
	void paintEvent(QPaintEvent *) override
	{
		QPainter p(this);
		p.setRenderHint(QPainter::Antialiasing);

		int margin = 2;
		QRectF rect(margin, margin, width() - 2 * margin,
			    height() - 2 * margin);
		int pen_width = 3;

		/* Background ring */
		QPen bg_pen(QColor(60, 60, 60), pen_width, Qt::SolidLine,
			    Qt::RoundCap);
		p.setPen(bg_pen);
		p.drawArc(rect, 0, 360 * 16);

		if (!state.has_filter)
			return;

		float fill = 0.0f;
		QColor color;

		if (state.in_delay) {
			fill = state.delay_progress;
			color = QColor(160, 160, 160); /* gray during delay */
		} else if (state.animating) {
			fill = state.progress;
			color = QColor(76, 175, 80); /* green during animation */
		} else {
			/* Idle: show full ring if zoomed out, empty if zoomed in */
			fill = state.is_zoomed_out ? 1.0f : 0.0f;
			color = QColor(76, 175, 80);
		}

		if (fill > 0.001f) {
			QPen fg_pen(color, pen_width, Qt::SolidLine,
				    Qt::RoundCap);
			p.setPen(fg_pen);
			/* Qt arcs: start at 12 o'clock (90*16), sweep clockwise (negative) */
			int start = 90 * 16;
			int span = (int)(-fill * 360.0f * 16.0f);
			p.drawArc(rect, start, span);
		}
	}

private:
	squeezeback_dock_state state = {};
};

/* ──────────────────────────────────────────────
 * Dock Widget
 * ────────────────────────────────────────────── */

class SqueezebackDock : public QWidget {
public:
	SqueezebackDock(QWidget *parent = nullptr) : QWidget(parent)
	{
		auto *layout = new QVBoxLayout(this);
		layout->setContentsMargins(8, 8, 8, 8);
		layout->setSpacing(6);

		/* Progress ring + status label row */
		auto *top_row = new QHBoxLayout();
		top_row->setSpacing(8);

		ring = new ProgressRing(this);
		top_row->addWidget(ring);

		status_label = new QLabel("Idle", this);
		status_label->setStyleSheet("color: #aaa; font-size: 11px;");
		top_row->addWidget(status_label, 1);

		layout->addLayout(top_row);

		/* Duration row */
		auto *dur_row = new QHBoxLayout();
		dur_row->setSpacing(6);
		auto *dur_label = new QLabel("Duration", this);
		dur_label->setStyleSheet("color: #aaa; font-size: 11px;");
		dur_row->addWidget(dur_label);

		duration_spin = new QDoubleSpinBox(this);
		duration_spin->setRange(0.1, 10.0);
		duration_spin->setSingleStep(0.1);
		duration_spin->setDecimals(1);
		duration_spin->setSuffix(" s");
		duration_spin->setValue(1.8);
		dur_row->addWidget(duration_spin);
		layout->addLayout(dur_row);

		connect(duration_spin,
			QOverload<double>::of(&QDoubleSpinBox::valueChanged),
			this, [](double val) {
				squeezeback_set_duration((float)val);
			});

		/* Delay row */
		auto *delay_row = new QHBoxLayout();
		delay_row->setSpacing(6);
		auto *delay_label = new QLabel("Delay", this);
		delay_label->setStyleSheet("color: #aaa; font-size: 11px;");
		delay_row->addWidget(delay_label);

		delay_spin = new QDoubleSpinBox(this);
		delay_spin->setRange(0.0, 10.0);
		delay_spin->setSingleStep(0.1);
		delay_spin->setDecimals(1);
		delay_spin->setSuffix(" s");
		delay_spin->setValue(1.0);
		delay_row->addWidget(delay_spin);
		layout->addLayout(delay_row);

		connect(delay_spin,
			QOverload<double>::of(&QDoubleSpinBox::valueChanged),
			this, [](double val) {
				squeezeback_set_delay((float)val);
			});

		/* Toggle button */
		toggle_btn = new QPushButton("Toggle Squeeze", this);
		toggle_btn->setMinimumHeight(32);
		toggle_btn->setStyleSheet(
			"QPushButton {"
			"  background-color: #333;"
			"  color: #fff;"
			"  border: 1px solid #555;"
			"  border-radius: 4px;"
			"  padding: 6px 12px;"
			"  font-weight: bold;"
			"}"
			"QPushButton:hover {"
			"  background-color: #444;"
			"}"
			"QPushButton:pressed {"
			"  background-color: #222;"
			"}");
		connect(toggle_btn, &QPushButton::clicked, this, []() {
			squeezeback_trigger_toggle();
		});
		layout->addWidget(toggle_btn);

		layout->addStretch();

		/* Poll timer at ~30fps */
		poll_timer = new QTimer(this);
		connect(poll_timer, &QTimer::timeout, this,
			&SqueezebackDock::pollState);
		poll_timer->start(33);
	}

	~SqueezebackDock() {}

private:
	void pollState()
	{
		squeezeback_dock_state s;
		squeezeback_get_dock_state(&s);
		ring->setState(s);

		/* Sync spin boxes with filter values (only when user isn't editing) */
		if (s.has_filter) {
			if (!duration_spin->hasFocus()) {
				duration_spin->blockSignals(true);
				duration_spin->setValue((double)s.duration);
				duration_spin->blockSignals(false);
			}
			if (!delay_spin->hasFocus()) {
				delay_spin->blockSignals(true);
				delay_spin->setValue((double)s.delay);
				delay_spin->blockSignals(false);
			}
			duration_spin->setEnabled(true);
			delay_spin->setEnabled(true);
		} else {
			duration_spin->setEnabled(false);
			delay_spin->setEnabled(false);
		}

		if (!s.has_filter) {
			status_label->setText("No active filter");
			status_label->setStyleSheet(
				"color: #777; font-size: 11px;");
		} else if (s.in_delay) {
			status_label->setText("Delay...");
			status_label->setStyleSheet(
				"color: #aaa; font-size: 11px;");
		} else if (s.animating) {
			status_label->setText("Animating");
			status_label->setStyleSheet(
				"color: #4CAF50; font-size: 11px;");
		} else if (s.is_zoomed_out) {
			status_label->setText("Zoomed out");
			status_label->setStyleSheet(
				"color: #aaa; font-size: 11px;");
		} else {
			status_label->setText("Zoomed in");
			status_label->setStyleSheet(
				"color: #aaa; font-size: 11px;");
		}
	}

	ProgressRing *ring;
	QPushButton *toggle_btn;
	QLabel *status_label;
	QDoubleSpinBox *duration_spin;
	QDoubleSpinBox *delay_spin;
	QTimer *poll_timer;
};

/* ──────────────────────────────────────────────
 * C-linkage init/destroy (called from plugin-main.c)
 * ────────────────────────────────────────────── */

static SqueezebackDock *dock_widget = nullptr;

void squeezeback_dock_init(void)
{
	dock_widget = new SqueezebackDock();
	obs_frontend_add_dock_by_id("squeezeback-dock", "Squeezeback",
				    dock_widget);
}

void squeezeback_dock_destroy(void)
{
	if (dock_widget) {
		obs_frontend_remove_dock("squeezeback-dock");
		dock_widget = nullptr;
	}
}
