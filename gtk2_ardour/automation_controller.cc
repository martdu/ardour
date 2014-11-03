/*
    Copyright (C) 2007 Paul Davis
    Author: David Robillard

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

*/

#include <iomanip>
#include <cmath>

#include "pbd/compose.h"
#include "pbd/error.h"

#include "ardour/automatable.h"
#include "ardour/automation_control.h"
#include "ardour/session.h"
#include "ardour/tempo.h"

#include "ardour_ui.h"
#include "automation_controller.h"
#include "gui_thread.h"
#include "note_select_dialog.h"

#include "i18n.h"

using namespace ARDOUR;
using namespace Gtk;

AutomationController::AutomationController(boost::shared_ptr<Automatable>       printer,
                                           boost::shared_ptr<AutomationControl> ac,
                                           Adjustment*                          adj)
	: BarController (*adj, ac)
	, _ignore_change(false)
	, _printer (printer)
	, _controllable(ac)
	, _adjustment(adj)
{
	assert (_printer);

	set_name (X_("ProcessorControlSlider"));

	StartGesture.connect (sigc::mem_fun(*this, &AutomationController::start_touch));
	StopGesture.connect (sigc::mem_fun(*this, &AutomationController::end_touch));

	signal_button_release_event().connect(
		sigc::mem_fun(*this, &AutomationController::on_button_release));

	_adjustment->signal_value_changed().connect (
			sigc::mem_fun(*this, &AutomationController::value_adjusted));

	_screen_update_connection = ARDOUR_UI::RapidScreenUpdate.connect (
			sigc::mem_fun (*this, &AutomationController::display_effective_value));

	ac->Changed.connect (_changed_connection, invalidator (*this), boost::bind (&AutomationController::value_changed, this), gui_context());
}

AutomationController::~AutomationController()
{
}

boost::shared_ptr<AutomationController>
AutomationController::create(boost::shared_ptr<Automatable>       printer,
                             const Evoral::Parameter&             param,
                             const ParameterDescriptor&           desc,
                             boost::shared_ptr<AutomationControl> ac)
{
	const double lo        = ac->internal_to_interface(desc.lower);
	const double up        = ac->internal_to_interface(desc.upper);
	const double normal    = ac->internal_to_interface(desc.normal);
	double       smallstep = desc.smallstep;
	double       largestep = desc.largestep;
	if (smallstep == 0.0) {
		smallstep = (up - lo) / 100;
	}
	if (largestep == 0.0) {
		largestep = (up - lo) / 10;
	}
	smallstep = ac->internal_to_interface(smallstep);
	largestep = ac->internal_to_interface(largestep);

	Gtk::Adjustment* adjustment = manage (
		new Gtk::Adjustment (normal, lo, up, smallstep, largestep));

	assert (ac);
	assert(ac->parameter() == param);
	return boost::shared_ptr<AutomationController>(new AutomationController(printer, ac, adjustment));
}

std::string
AutomationController::get_label (double& xpos)
{
        xpos = 0.5;
        return _printer->value_as_string (_controllable);
}

void
AutomationController::display_effective_value()
{
	double const interface_value = _controllable->internal_to_interface(_controllable->get_value());

	if (_adjustment->get_value () != interface_value) {
		_ignore_change = true;
		_adjustment->set_value (interface_value);
		_ignore_change = false;
	}
}

void
AutomationController::value_adjusted ()
{
	if (!_ignore_change) {
		_controllable->set_value (_controllable->interface_to_internal(_adjustment->get_value()));
	}
}

void
AutomationController::start_touch()
{
	_controllable->start_touch (_controllable->session().transport_frame());
}

void
AutomationController::end_touch ()
{
	if (!_controllable->alist()) return;
	if (_controllable->automation_state() == Touch) {

		bool mark = false;
		double when = 0;

		if (_controllable->session().transport_rolling()) {
			mark = true;
			when = _controllable->session().transport_frame();
		}

		_controllable->stop_touch (mark, when);
	}
}

void
AutomationController::run_note_select_dialog()
{
	NoteSelectDialog* dialog = new NoteSelectDialog();
	if (dialog->run() == Gtk::RESPONSE_ACCEPT) {
		_controllable->set_value(dialog->note_number());
	}
	delete dialog;
}

void
AutomationController::set_freq_beats(double beats)
{
	const ARDOUR::Session& session = _controllable->session();
	const ARDOUR::Tempo&   tempo   = session.tempo_map().tempo_at(0);
	const double           bpm     = tempo.beats_per_minute();
	const double           bps     = bpm / 60.0;
	_controllable->set_value(bps / beats);
}

void
AutomationController::set_ratio(double ratio)
{
	_controllable->set_value(_controllable->get_value() * ratio);
}

bool
AutomationController::on_button_release(GdkEventButton* ev)
{
	using namespace Gtk::Menu_Helpers;

	if (ev->button != 3) {
		return false;
	}

	if (_controllable->desc().unit == ARDOUR::ParameterDescriptor::MIDI_NOTE) {
		Gtk::Menu* menu  = manage(new Menu());
		MenuList&  items = menu->items();
		items.push_back(MenuElem(_("Select Note..."),
		                         sigc::mem_fun(*this, &AutomationController::run_note_select_dialog)));
		menu->popup(1, ev->time);
		return true;
	} else if (_controllable->desc().unit == ARDOUR::ParameterDescriptor::HZ) {
		Gtk::Menu* menu  = manage(new Menu());
		MenuList&  items = menu->items();
		items.push_back(MenuElem(_("Halve"),
		                         sigc::bind(sigc::mem_fun(*this, &AutomationController::set_ratio),
		                                    0.5)));
		items.push_back(MenuElem(_("Double"),
		                         sigc::bind(sigc::mem_fun(*this, &AutomationController::set_ratio),
		                                    2.0)));
		for (double beats = 1.0; beats <= 16; ++beats) {
			items.push_back(MenuElem(string_compose(_("Set to %1 beat(s)"), (int)beats),
			                         sigc::bind(sigc::mem_fun(*this, &AutomationController::set_freq_beats),
			                                    beats)));
		}
		menu->popup(1, ev->time);
		return true;
	}

	return false;
}

void
AutomationController::value_changed ()
{
	Gtkmm2ext::UI::instance()->call_slot (invalidator (*this), boost::bind (&AutomationController::display_effective_value, this));
}

/** Stop updating our value from our controllable */
void
AutomationController::stop_updating ()
{
	_screen_update_connection.disconnect ();
}
