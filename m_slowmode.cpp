//
// Created by foxlet on 1/18/16.
//

/*
 * InspIRCd -- Internet Relay Chat Daemon
 *
 *   Copyright (C) 2009 Daniel De Graaf <danieldg@inspircd.org>
 *   Copyright (C) 2008 Pippijn van Steenhoven <pip88nl@gmail.com>
 *   Copyright (C) 2007 Robin Burchell <robin+git@viroteck.net>
 *   Copyright (C) 2007 John Brooks <john.brooks@dereferenced.net>
 *   Copyright (C) 2007 Dennis Friis <peavey@inspircd.org>
 *   Copyright (C) 2006 Craig Edwards <craigedwards@brainbox.cc>
 *   Copyright (C) 2006 Oliver Lupton <oliverlupton@gmail.com>
 *
 * This file is part of InspIRCd.  InspIRCd is free software: you can
 * redistribute it and/or modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation, version 2.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */


#include "inspircd.h"

/* $ModDesc: Provides channel mode +U (enables snoonet slowmode) */

/** Holds flag settings and state for mode +U
 */
class slmodsettings
{
public:
    unsigned int secs;
    unsigned int lines;
    time_t reset;
    std::map<User*, unsigned int> counters;

    slmodsettings(int b, int c) : secs(b), lines(c)
    {
        reset = ServerInstance->Time() + secs;
    }

    bool addmessage(User* who)
    {
        if (ServerInstance->Time() > reset)
        {
            counters.clear();
            reset = ServerInstance->Time() + secs;
        }

        return (++counters[who] >= this->lines);
    }

    void clear(User* who)
    {
        std::map<User*, unsigned int>::iterator iter = counters.find(who);
        if (iter != counters.end())
        {
            counters.erase(iter);
        }
    }
};

/** Handles channel mode +U
 */
class SlowMode : public ModeHandler
{
public:
    SimpleExtItem<slmodsettings> ext;
    SlowMode(Module* Creator) : ModeHandler(Creator, "slowmode", 'U', PARAM_SETONLY, MODETYPE_CHANNEL),
                                ext("slowmode", Creator) { }

    ModeAction OnModeChange(User* source, User* dest, Channel* channel, std::string &parameter, bool adding)
    {
        if (adding)
        {
            std::string::size_type colon = parameter.find(':');
            if ((colon == std::string::npos) || (parameter.find('-') != std::string::npos))
            {
                source->WriteNumeric(608, "%s %s :Invalid slowmode parameter",source->nick.c_str(),channel->name.c_str());
                return MODEACTION_DENY;
            }

            /* Set up the slowmode parameters for this channel */
            unsigned int nlines = ConvToInt(parameter.substr(0, colon));
            unsigned int nsecs = ConvToInt(parameter.substr(colon+1));

            if ((nlines<2) || (nsecs<1))
            {
                source->WriteNumeric(608, "%s %s :Invalid slowmode parameter",source->nick.c_str(),channel->name.c_str());
                return MODEACTION_DENY;
            }

            slmodsettings* f = ext.get(channel);
            if ((f) && (nlines == f->lines) && (nsecs == f->secs))
                // mode params match
                return MODEACTION_DENY;

            ext.set(channel, new slmodsettings(nsecs, nlines));
            parameter = std::string("") + ConvToStr(nlines) + ":" + ConvToStr(nsecs);
            channel->SetModeParam('U', parameter);
            return MODEACTION_ALLOW;
        }
        else
        {
            if (!channel->IsModeSet('U'))
                return MODEACTION_DENY;

            ext.unset(channel);
            channel->SetModeParam('U', "");
            return MODEACTION_ALLOW;
        }
    }
};

class ModuleSlowMode : public Module
{
    SlowMode ml;

public:

    ModuleSlowMode()
            : ml(this)
    {
    }

    void init()
    {
        ServerInstance->Modules->AddService(ml);
        ServerInstance->Modules->AddService(ml.ext);
        Implementation eventlist[] = { I_OnUserPreNotice, I_OnUserPreMessage };
        ServerInstance->Modules->Attach(eventlist, this, sizeof(eventlist)/sizeof(Implementation));
    }

    ModResult ProcessMessages(User* user,Channel* dest, const std::string &text)
    {
        if ((!IS_LOCAL(user)) || !dest->IsModeSet('U'))
            return MOD_RES_PASSTHRU;

        if (ServerInstance->OnCheckExemption(user,dest,"slowmode") == MOD_RES_ALLOW)
            return MOD_RES_PASSTHRU;

        slmodsettings *f = ml.ext.get(dest);
        Channel* chan = static_cast<Channel*>(dest);

        if (f)
        {
            if (f->addmessage(user))
            {
                /* Simply deny to send the message. */
                char warnMessage[MAXBUF];
                snprintf(warnMessage, MAXBUF, "Cannot send message to channel. You are throttled. You may only send %u lines in %u seconds.", f->lines, f->secs);

                user->WriteNumeric(404, "%s %s :%s", user->nick.c_str(), chan->name.c_str(), warnMessage);

                return MOD_RES_DENY;
            }
        }

        return MOD_RES_PASSTHRU;
    }

    ModResult OnUserPreMessage(User *user, void *dest, int target_type, std::string &text, char status, CUList &exempt_list)
    {
        if (target_type == TYPE_CHANNEL)
            return ProcessMessages(user,(Channel*)dest,text);

        return MOD_RES_PASSTHRU;
    }

    ModResult OnUserPreNotice(User *user, void *dest, int target_type, std::string &text, char status, CUList &exempt_list)
    {
        if (target_type == TYPE_CHANNEL)
            return ProcessMessages(user,(Channel*)dest,text);

        return MOD_RES_PASSTHRU;
    }

    void Prioritize()
    {
        // we want to be after all modules that might deny the message (e.g. m_muteban, m_noctcp, m_blockcolor, etc.)
        ServerInstance->Modules->SetPriority(this, I_OnUserPreMessage, PRIORITY_LAST);
        ServerInstance->Modules->SetPriority(this, I_OnUserPreNotice, PRIORITY_LAST);
    }

    Version GetVersion()
    {
        return Version("Provides channel mode +U (enables snoonet slowmode)", VF_VENDOR);
    }
};

MODULE_INIT(ModuleSlowMode)
