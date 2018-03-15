/* $ModDesc: Adds a command to forcibly sync all channel modes across the network. */
/* $ModAuthor: linuxdaemon */
/* $ModAuthorMail: linuxdaemon@snoonet.org */
/* $ModDepends: core 2.0 */

#include "inspircd.h"

namespace
{
	typedef std::pair<char, std::string> mode_data_t;
	typedef std::vector<mode_data_t> mode_list;
}

class CommandSyncModes : public Command
{
	void SendModes(Channel *c, User *u, const mode_list &modes)
	{
		irc::modestacker stack(true);
		ModeHandler *mh;
		std::vector<TranslateType> translate;
		translate.push_back(TR_TEXT);
		std::string modechars;
		parameterlist params;
		std::string param;

		for (mode_list::const_iterator it = modes.begin(), it_end = modes.end(); it != it_end; ++it)
		{
			mh = ServerInstance->Modes->FindMode(it->first, MODETYPE_CHANNEL);
			if (!mh)
				continue;

			param = it->second;

			if (param.empty())
				stack.Push(mh->GetModeChar());
			else
			{
				stack.Push(mh->GetModeChar(), param);
				translate.push_back(mh->GetTranslateType());
			}
		}

		std::vector<std::string> stackresult;
		while (stack.GetStackedLine(stackresult))
		{
			std::vector<TranslateType> tr(translate.begin(), translate.begin() + (stackresult.size() - 1));
			ServerInstance->PI->SendMode(c->name, stackresult, tr);
		}
	}

 public:
	CommandSyncModes(Module *parent) : Command(parent, "SYNCMODES")
	{
		flags_needed = 'o';
	}

	CmdResult Handle(const std::vector<std::string>& parameters, User *user)
	{
		for (chan_hash::iterator it = ServerInstance->chanlist->begin(); it != ServerInstance->chanlist->end(); ++it)
		{
			Channel *c = it->second;
			mode_list modes;

			for (BanList::iterator b_it = c->bans.begin(); b_it != c->bans.end(); ++b_it)
				modes.push_back(std::make_pair('b', b_it->data));

			irc::spacesepstream sstr(c->ChanModes(true));
			std::string modechars;
			sstr.GetToken(modechars);
			parameterlist params;
			std::string tmp;

			while (sstr.GetToken(tmp))
				params.push_back(tmp);

			parameterlist::const_iterator param_iter = params.begin();

			for (std::string::const_iterator m_it = modechars.begin(), m_it_end = modechars.end(); m_it != m_it_end; ++m_it)
			{
				ModeHandler *mh = ServerInstance->Modes->FindMode(*m_it, MODETYPE_CHANNEL);
				if (!mh)
					continue;

				if (mh->GetNumParams(true) && param_iter != params.end())
					modes.push_back(std::make_pair(mh->GetModeChar(), *(param_iter++)));
				else
					modes.push_back(std::make_pair(mh->GetModeChar(), ""));
			}

			this->SendModes(c, user, modes);

			FOREACH_MOD(I_OnSyncChannel,OnSyncChannel(c, creator, user));
		}
		return CMD_SUCCESS;
	}

	RouteDescriptor GetRouting(User* user, const std::vector<std::string>& parameters)
	{
		return ROUTE_BROADCAST;
	}
};

class ModuleSyncModes : public Module
{
	CommandSyncModes cmd;

 public:
	ModuleSyncModes() : cmd(this)
	{
	}

	void ProtoSendMode(void*, TargetTypeFlags, void* target, const std::vector<std::string>& result, const std::vector<TranslateType>& translate)
	{
		Channel *c = static_cast<Channel *>(target);
		ServerInstance->PI->SendMode(c->name, result, translate);
	}

	void init()
	{
		ServerInstance->Modules->AddService(cmd);
	}

	Version GetVersion()
	{
		return Version("Adds a command to forcibly sync all channel modes", VF_COMMON);
	}
};

MODULE_INIT(ModuleSyncModes)
