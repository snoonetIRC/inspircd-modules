/*
 * InspIRCd -- Internet Relay Chat Daemon
 *
 *   Copyright (C) 2017 Attila Molnar <attilamolnar@hush.com>
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

/* $ModDepends: core 2.0 */

#include "inspircd.h"

class ScoreExt : public LocalIntExt
{
	std::string serialize(SerializeFormat format, const Extensible* container, void* item) const
	{
		return LocalIntExt::serialize(FORMAT_USER, container, item);
	}

	void unserialize(SerializeFormat format, Extensible* container, const std::string& value)
	{
		set(container, ConvToInt(value));
	}

 public:
	ScoreExt(Module* mod)
		: LocalIntExt("score", mod)
	{
	}
};

class CommandScore : public Command
{
 public:
	ScoreExt ext;

	CommandScore(Module* mod)
		: Command(mod, "SCORE", 1)
		, ext(mod)
	{
		flags_needed = 'o';
		syntax = "<nick> [<score>]";
	}

	CmdResult Handle(const std::vector<std::string>& parameters, User* user)
	{
		User* const target = ServerInstance->FindNick(parameters.front());
		if (!target)
			return CMD_FAILURE;

		int score = (int)ext.get(target);
		if (parameters.size() < 2)
		{
			user->WriteNumeric(810, "%s %s %d", user->nick.c_str(), target->nick.c_str(), score);
			return CMD_SUCCESS;
		}

		int newscore = ConvToInt(parameters[1]);
		if (score == newscore)
			return CMD_SUCCESS;

		ext.set(target, newscore);
		ServerInstance->PI->SendMetaData(target, ext.name, ConvToStr(newscore));

		return CMD_SUCCESS;
	}
};

class ModuleUserScore : public Module
{
	CommandScore cmd;

 public:
	ModuleUserScore()
		: cmd(this)
	{
	}

	void init()
	{
		ServerInstance->Modules->AddService(cmd);
		ServerInstance->Modules->AddService(cmd.ext);
		Implementation eventlist[] = { I_OnWhois, I_On005Numeric, I_OnCheckBan };
		ServerInstance->Modules->Attach(eventlist, this, sizeof(eventlist)/sizeof(Implementation));
	}

	void OnWhois(User* user, User* dest)
	{
		if (!user->HasPrivPermission("users/auspex"))
			return;

		ServerInstance->SendWhoisLine(user, dest, 320, "%s %s :has score %d", user->nick.c_str(), dest->nick.c_str(), (int)cmd.ext.get(dest));
	}

	ModResult OnCheckBan(User* user, Channel* chan, const std::string& mask)
	{
		if ((mask.length() <= 2) || (mask[0] != 's') || (mask[1] != ':'))
			return MOD_RES_PASSTHRU;

		int required_score = ConvToInt(mask.substr(2));
		if (cmd.ext.get(user) < required_score)
		{
			// user->WriteNumeric(609, "%s %s :You cannot join because your user score is too low", user->nick.c_str(), chan->name.c_str());
			return MOD_RES_DENY;
		}

		return MOD_RES_PASSTHRU;
	}

	void On005Numeric(std::string& tokens)
	{
		ServerInstance->AddExtBanChar('s');
	}

	Version GetVersion()
	{
		return Version("Provides support for setting the score of users and restricting them based on that", VF_COMMON);
	}
};

MODULE_INIT(ModuleUserScore)
