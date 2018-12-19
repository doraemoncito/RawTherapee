/*
 *  This file is part of RawTherapee.
 *
 *  Copyright (c) 2004-2010 Gabor Horvath <hgabor@rawtherapee.com>
 *
 *  RawTherapee is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  RawTherapee is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with RawTherapee.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "eventmapper.h"
#include "xtransprocess.h"
#include "options.h"
#include "guiutils.h"

using namespace rtengine;
using namespace rtengine::procparams;

XTransProcess::XTransProcess () : FoldableToolPanel(this, "xtransprocess", M("TP_RAW_LABEL"), true)
{
    auto m = ProcEventMapper::getInstance();
    EvDemosaicContrast = m->newEvent(DEMOSAIC, "HISTORY_MSG_DUALDEMOSAIC_CONTRAST");
    EvDemosaicSmoothGreens = m->newEvent(DEMOSAIC, "HISTORY_MSG_FASTDEMOSAIC_SMOOTH");
    EvDemosaicAutoContrast = m->newEvent(DEMOSAIC, "HISTORY_MSG_DUALDEMOSAIC_AUTO_CONTRAST");

    Gtk::HBox* hb1 = Gtk::manage (new Gtk::HBox ());
    hb1->pack_start (*Gtk::manage (new Gtk::Label ( M("TP_RAW_DMETHOD") + ": ")), Gtk::PACK_SHRINK, 4);
    method = Gtk::manage (new MyComboBoxText ());

    for (const auto method_string : RAWParams::XTransSensor::getMethodStrings()) {
        const std::string langKey =
            [method_string]() -> std::string
            {
                const std::string str(method_string);

                std::string res;
                for (const auto& c : str) {
                    switch (c) {
                        case '(':
                        case ')':
                        case ' ':
                        case '-': {
                            continue;
                        }

                        default: {
                            res += c;
                            break;
                        }
                    }
                }

                return res;
            }();
        method->append(M("TP_RAW_" + Glib::ustring(langKey).uppercase()));
    }

    method->set_active(0);
    hb1->set_tooltip_markup (M("TP_RAW_SENSOR_XTRANS_DMETHOD_TOOLTIP"));

    hb1->pack_end (*method, Gtk::PACK_EXPAND_WIDGET, 4);
    pack_start( *hb1, Gtk::PACK_SHRINK, 4);

    dualDemosaicOptions = Gtk::manage (new Gtk::VBox ());

    dualDemosaicContrast = Gtk::manage(new Adjuster (M("TP_RAW_DUALDEMOSAICCONTRAST"), 0, 100, 1, 20));
    dualDemosaicContrast->setAdjusterListener (this);
    dualDemosaicContrast->addAutoButton(M("TP_RAW_DUALDEMOSAICAUTOCONTRAST_TOOLTIP"));
    dualDemosaicContrast->setAutoValue(true);

    if (dualDemosaicContrast->delay < options.adjusterMaxDelay) {
        dualDemosaicContrast->delay = options.adjusterMaxDelay;
    }

    dualDemosaicContrast->show();
    dualDemosaicOptions->pack_start(*dualDemosaicContrast);

    smoothGreens = Gtk::manage(new Adjuster (M("TP_RAW_FASTDEMOSAIC_SMOOTH"), 0, 100, 1, 0));
    smoothGreens->setAdjusterListener (this);

    if (smoothGreens->delay < options.adjusterMaxDelay) {
        smoothGreens->delay = options.adjusterMaxDelay;
    }

    smoothGreens->show();
    dualDemosaicOptions->pack_start(*smoothGreens);

    pack_start( *dualDemosaicOptions, Gtk::PACK_SHRINK, 4);

    pack_start( *Gtk::manage( new Gtk::HSeparator()), Gtk::PACK_SHRINK, 0 );
    ccSteps = Gtk::manage (new Adjuster (M("TP_RAW_FALSECOLOR"), 0, 5, 1, 0 ));
    ccSteps->setAdjusterListener (this);

    if (ccSteps->delay < options.adjusterMaxDelay) {
        ccSteps->delay = options.adjusterMaxDelay;
    }

    ccSteps->show();
    pack_start( *ccSteps, Gtk::PACK_SHRINK, 4);

    methodconn = method->signal_changed().connect( sigc::mem_fun(*this, &XTransProcess::methodChanged) );
}

XTransProcess::~XTransProcess () {
    idle_register.destroy();
}

void XTransProcess::read(const rtengine::procparams::ProcParams* pp, const ParamsEdited* pedited)
{
    disableListener ();
    methodconn.block (true);

    for (size_t i = 0; i < RAWParams::XTransSensor::getMethodStrings().size(); ++i)
        if( pp->raw.xtranssensor.method == RAWParams::XTransSensor::getMethodStrings()[i]) {
            method->set_active(i);
            oldSelection = i;
            break;
        }

    if(pedited ) {
        dualDemosaicContrast->setAutoInconsistent   (multiImage && !pedited->raw.xtranssensor.dualDemosaicAutoContrast);
        dualDemosaicContrast->setEditedState ( pedited->raw.xtranssensor.dualDemosaicContrast ? Edited : UnEdited);
        smoothGreens->setEditedState ( pedited->raw.xtranssensor.smoothGreens ? Edited : UnEdited);
        ccSteps->setEditedState (pedited->raw.xtranssensor.ccSteps ? Edited : UnEdited);

        if( !pedited->raw.xtranssensor.method ) {
            method->set_active_text(M("GENERAL_UNCHANGED"));
        }
    }
    dualDemosaicContrast->setAutoValue(pp->raw.xtranssensor.dualDemosaicAutoContrast);
    dualDemosaicContrast->setValue (pp->raw.xtranssensor.dualDemosaicContrast);
    smoothGreens->setValue (pp->raw.xtranssensor.smoothGreens);
    ccSteps->setValue (pp->raw.xtranssensor.ccSteps);

    lastAutoContrast = pp->raw.bayersensor.dualDemosaicAutoContrast;

    if (!batchMode) {
        dualDemosaicOptions->set_visible(pp->raw.xtranssensor.method == procparams::RAWParams::XTransSensor::getMethodString(procparams::RAWParams::XTransSensor::Method::FOUR_PASS)
                                         || pp->raw.xtranssensor.method == procparams::RAWParams::XTransSensor::getMethodString(procparams::RAWParams::XTransSensor::Method::TWO_PASS));
    }

    methodconn.block (false);

    enableListener ();
}

void XTransProcess::write( rtengine::procparams::ProcParams* pp, ParamsEdited* pedited)
{
    pp->raw.xtranssensor.dualDemosaicAutoContrast = dualDemosaicContrast->getAutoValue();
    pp->raw.xtranssensor.dualDemosaicContrast = dualDemosaicContrast->getValue();
    pp->raw.xtranssensor.smoothGreens = smoothGreens->getValue();
    pp->raw.xtranssensor.ccSteps = ccSteps->getIntValue();

    int currentRow = method->get_active_row_number();

    if (currentRow >= 0 && method->get_active_text() != M("GENERAL_UNCHANGED")) {
        pp->raw.xtranssensor.method = procparams::RAWParams::XTransSensor::getMethodStrings()[currentRow];
    }

    if (pedited) {
        pedited->raw.xtranssensor.method = method->get_active_text() != M("GENERAL_UNCHANGED");
        pedited->raw.xtranssensor.dualDemosaicAutoContrast = !dualDemosaicContrast->getAutoInconsistent ();
        pedited->raw.xtranssensor.dualDemosaicContrast = dualDemosaicContrast->getEditedState ();
        pedited->raw.xtranssensor.smoothGreens = smoothGreens->getEditedState ();
        pedited->raw.xtranssensor.ccSteps = ccSteps->getEditedState ();
    }
}

void XTransProcess::setAdjusterBehavior (bool falsecoloradd, bool dualDemosaicContrastAdd)
{
    dualDemosaicContrast->setAddMode(dualDemosaicContrastAdd);
    smoothGreens->setAddMode(dualDemosaicContrastAdd);
    ccSteps->setAddMode(falsecoloradd);
}

void XTransProcess::setBatchMode(bool batchMode)
{
    method->append (M("GENERAL_UNCHANGED"));
    method->set_active_text(M("GENERAL_UNCHANGED"));
    ToolPanel::setBatchMode (batchMode);
    dualDemosaicContrast->showEditedCB ();
    smoothGreens->showEditedCB ();
    ccSteps->showEditedCB ();
}

void XTransProcess::setDefaults(const rtengine::procparams::ProcParams* defParams, const ParamsEdited* pedited)
{
    dualDemosaicContrast->setDefault( defParams->raw.xtranssensor.dualDemosaicContrast);
    smoothGreens->setDefault( defParams->raw.xtranssensor.smoothGreens);
    ccSteps->setDefault (defParams->raw.xtranssensor.ccSteps);

    if (pedited) {
        dualDemosaicContrast->setDefaultEditedState( pedited->raw.xtranssensor.dualDemosaicContrast ? Edited : UnEdited);
        smoothGreens->setDefaultEditedState( pedited->raw.xtranssensor.smoothGreens ? Edited : UnEdited);
        ccSteps->setDefaultEditedState(pedited->raw.xtranssensor.ccSteps ? Edited : UnEdited);
    } else {
        dualDemosaicContrast->setDefaultEditedState(Irrelevant );
        smoothGreens->setDefaultEditedState(Irrelevant );
        ccSteps->setDefaultEditedState(Irrelevant );
    }
}

void XTransProcess::adjusterChanged(Adjuster* a, double newval)
{
    if (listener) {
        if (a == ccSteps) {
            listener->panelChanged (EvDemosaicFalseColorIter, a->getTextValue() );
        } else if (a == dualDemosaicContrast) {
            listener->panelChanged (EvDemosaicContrast, a->getTextValue() );
        } else if (a == smoothGreens) {
            listener->panelChanged (EvDemosaicSmoothGreens, a->getTextValue() );
        }
    }
}

void XTransProcess::adjusterAutoToggled(Adjuster* a, bool newval)
{
    if (multiImage) {
        if (dualDemosaicContrast->getAutoInconsistent()) {
            dualDemosaicContrast->setAutoInconsistent (false);
            dualDemosaicContrast->setAutoValue (false);
        } else if (lastAutoContrast) {
            dualDemosaicContrast->setAutoInconsistent (true);
        }

        lastAutoContrast = dualDemosaicContrast->getAutoValue();
    }

    if (listener) {

        if (a == dualDemosaicContrast) {
            if (dualDemosaicContrast->getAutoInconsistent()) {
                listener->panelChanged (EvDemosaicAutoContrast, M ("GENERAL_UNCHANGED"));
            } else if (dualDemosaicContrast->getAutoValue()) {
                listener->panelChanged (EvDemosaicAutoContrast, M ("GENERAL_ENABLED"));
            } else {
                listener->panelChanged (EvDemosaicAutoContrast, M ("GENERAL_DISABLED"));
            }
        }
    }
}

void XTransProcess::methodChanged ()
{
    const int curSelection = method->get_active_row_number();
    const RAWParams::XTransSensor::Method currentMethod = RAWParams::XTransSensor::Method(curSelection);

    oldSelection = curSelection;

    if (!batchMode) {
        if (currentMethod == procparams::RAWParams::XTransSensor::Method::FOUR_PASS || currentMethod == procparams::RAWParams::XTransSensor::Method::TWO_PASS) {
            dualDemosaicOptions->show();
        } else {
            dualDemosaicOptions->hide();
        }

    }
    if (listener && method->get_active_row_number() >= 0) {
        listener->panelChanged (
            currentMethod == RAWParams::XTransSensor::Method::MONO || RAWParams::XTransSensor::Method(oldSelection) == RAWParams::XTransSensor::Method::MONO
            ? EvDemosaicMethodPreProc
            : EvDemosaicMethod, method->get_active_text());
    }
}

void XTransProcess::checkBoxToggled (CheckBox* c, CheckValue newval)
{
}

void XTransProcess::autoContrastChanged (double autoContrast)
{
    struct Data {
        XTransProcess *me;
        double autoContrast;
    };
    const auto func = [](gpointer data) -> gboolean {
        Data *d = static_cast<Data *>(data);
        XTransProcess *me = d->me;
        me->disableListener();
        me->dualDemosaicContrast->setValue(d->autoContrast);
        me->enableListener();
        delete d;
        return FALSE;
    };

    idle_register.add(func, new Data { this, autoContrast });
}
