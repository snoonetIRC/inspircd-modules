/*
 * InspIRCd -- Internet Relay Chat Daemon
 *
 *   Copyright (C) 2018 linuxdaemon <linuxdaemon@snoonet.org>
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

/* $ModDesc: Provides channel mode +x (oper only top-level channel flood protection with SNOMASK +F) */
/* $ModDepends: core 2.0 */

typedef std::map<User*, unsigned int> counter_t;

/** Holds flood settings and state for mode +x
 */
class globalfloodsettings
{
 public:
	bool ban;
	unsigned int secs;
	unsigned int lines;
	time_t reset;
	counter_t counters;

	globalfloodsettings(bool a, int b, int c) : ban(a), secs(b), lines(c)
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
		counter_t::iterator iter = counters.find(who);
		if (iter != counters.end())
		{
			counters.erase(iter);
		}
	}
};

/** Handles channel mode +x
 */
class GlobalMsgFlood : public ModeHandler
{
 public:
	SimpleExtItem<globalfloodsettings> ext;
	/* This an oper only mode */
	GlobalMsgFlood(Module* Creator) : ModeHandler(Creator, "globalflood", 'x', PARAM_SETONLY, MODETYPE_CHANNEL),
		ext("globalmessageflood", Creator)
	{
		oper = true;
	}

	ModeAction OnModeChange(User* source, User* dest, Channel* channel, std::string &parameter, bool adding)
	{
		if (adding)
		{
			std::string::size_type colon = parameter.find(':');
			if ((colon == std::string::npos) || (parameter.find('-') != std::string::npos))
			{
				source->WriteNumeric(608, "%s %s :Invalid flood parameter",source->nick.c_str(),channel->name.c_str());
				return MODEACTION_DENY;
			}

			/* Set up the flood parameters for this channel */
			bool ban = (parameter[0] == '*');
			unsigned int nlines = ConvToInt(parameter.substr(ban ? 1 : 0, ban ? colon-1 : colon));
			unsigned int nsecs = ConvToInt(parameter.substr(colon+1));

			if ((nlines<2) || (nsecs<1))
			{
				source->WriteNumeric(608, "%s %s :Invalid flood parameter",source->nick.c_str(),channel->name.c_str());
				return MODEACTION_DENY;
			}

			globalfloodsettings* f = ext.get(channel);
			if ((f) && (nlines == f->lines) && (nsecs == f->secs) && (ban == f->ban))
				// mode params match
				return MODEACTION_DENY;

			ext.set(channel, new globalfloodsettings(ban, nsecs, nlines));
			parameter = std::string(ban ? "*" : "") + ConvToStr(nlines) + ":" + ConvToStr(nsecs);
			channel->SetModeParam('x', parameter);
			return MODEACTION_ALLOW;
		}
		else
		{
			if (!channel->IsModeSet('x'))
				return MODEACTION_DENY;

			if (IS_LOCAL(source) && !source->HasModePermission(this->GetModeChar(), this->GetModeType()))
			{
				source->WriteNumeric(ERR_NOPRIVILEGES, "%s %s :Permission Denied - Only operators may set channel mode x",source->nick.c_str(),channel->name.c_str());
				return MODEACTION_DENY;
			}

			ext.unset(channel);
			channel->SetModeParam('x', "");
			return MODEACTION_ALLOW;
		}
	}
};

class ModuleGlobalMsgFlood : public Module
{
	GlobalMsgFlood mf;

 public:

	ModuleGlobalMsgFlood()
		: mf(this)
	{
	}

	void init()
	{
		ServerInstance->Modules->AddService(mf);
		ServerInstance->Modules->AddService(mf.ext);

		/* Enables Flood announcements for everyone with +s +F */
		ServerInstance->SNO->EnableSnomask('F',"FLOODANNOUNCE");

		Implementation eventlist[] = { I_OnUserPreNotice, I_OnUserPreMessage };
		ServerInstance->Modules->Attach(eventlist, this, sizeof(eventlist)/sizeof(Implementation));
	}

	ModResult ProcessMessages(User* user,Channel* dest, const std::string &text)
	{
		if ((!IS_LOCAL(user)) || !dest->IsModeSet('x'))
			return MOD_RES_PASSTHRU;

		if (user->IsModeSet('o'))
			return MOD_RES_PASSTHRU;

		globalfloodsettings *f = mf.ext.get(dest);
		if (f)
		{
			if (f->addmessage(user))
			{
				f->clear(user);
				/* Generate the SNOTICE when someone triggers the flood limit */

				ServerInstance->SNO->WriteGlobalSno('F', "Global channel flood triggered by %s in %s (limit was %u lines in %u secs)",
													user->GetFullRealHost().c_str(), dest->name.c_str(), f->lines, f->secs);

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
		return Version("Provides channel mode +x (oper-only message flood protection)", VF_VENDOR);
	}
};

MODULE_INIT(ModuleGlobalMsgFlood)
