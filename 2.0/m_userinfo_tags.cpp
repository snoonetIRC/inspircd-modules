/* $ModDesc: Adds the ability for opers to associate certain 'tags' with a user. */
/* $ModAuthor: linuxdaemon */
/* $ModAuthorMail: linuxdaemon@snoonet.org */
/* $ModDepends: core 2.0 */

#include "inspircd.h"

#define EXTBAN_CHAR 't'

enum
{
	RPL_TAGS = 752,
	RPL_NOTAGS = 753
};

typedef std::set<std::string> UserInfo;
typedef std::vector<std::pair<std::string, bool> > TagInfo;

class UserInfoExt : public SimpleExtItem<UserInfo>
{
 public:
	UserInfoExt(Module *parent) : SimpleExtItem("user-info", parent)
	{
	}

	std::string serialize(SerializeFormat format, const Extensible* container, void* item) const
	{
		std::stringstream sstr;
		UserInfo* data = static_cast<UserInfo*>(item);
		if (data->empty())
			return "";

		for (UserInfo::const_iterator it = data->begin(), it_end = data->end(); it != it_end; ++it)
			sstr << ',' << *it;

		return sstr.str().substr(1);
	}

	void unserialize(SerializeFormat format, Extensible* container, const std::string& value)
	{
		UserInfo* info = new UserInfo;
		set(container, info);

		irc::commasepstream stream(value);
		std::string token;
		while (stream.GetToken(token))
		{
			if (token.empty())
				continue;

			info->insert(token);
		}
	}

	UserInfo *get_user(User *u)
	{
		UserInfo *info = this->get(u);
		if (!info)
		{
			info = new UserInfo;
			this->set(u, info);
		}

		return info;
	}
};

static TagInfo parseTagInfo(const std::string &text)
{
	TagInfo ti;
	irc::commasepstream stream(text);
	std::string token;
	while (stream.GetToken(token))
	{
		if (token.empty())
			continue;

		bool add;
		switch (token[0])
		{
			case '-':
				token.erase(0, 1);
				add = false;
				break;
			case '+':
				token.erase(0, 1);
			default:
				add = true;
				break;
		}

		ti.push_back(std::make_pair(token, add));
	}
	return ti;
}

class UserInfoCommand : public Command
{
 public:
	UserInfoExt ext;

	UserInfoCommand(Module *me) : Command(me, "USERINFO", 1), ext(me)
	{
		syntax = "<target> [{+|-}info]";
		flags_needed = 'o';
		TRANSLATE3(TR_NICK, TR_TEXT, TR_END);
	}

	CmdResult Handle(const parameterlist& parameters, User* user)
	{
		std::string target = parameters[0];
		User* target_user = ServerInstance->FindNick(target);
		if (!target_user)
		{
			user->WriteNumeric(ERR_NOSUCHNICK, "%s %s :No such nick", user->nick.c_str(), target.c_str());
			return CMD_FAILURE;
		}

		UserInfo* info = ext.get_user(target_user);

		std::string seralized = ext.serialize(FORMAT_USER, target_user, info);

		if (parameters.size() > 1)
		{
			TagInfo ti = parseTagInfo(parameters[1]);
			for (TagInfo::const_iterator it = ti.begin(), it_end = ti.end(); it != it_end; ++it)
			{
				if (it->second)
					info->insert(it->first);
				else
					info->erase(it->first);
			}

			seralized = ext.serialize(FORMAT_USER, target_user, info);

			ServerInstance->PI->SendMetaData(target_user, ext.name, seralized);
		}

		if (info->empty())
		{
			user->WriteNumeric(RPL_NOTAGS, "%s %s :has no tags", user->nick.c_str(), target_user->nick.c_str());
		}
		else
		{
			user->WriteNumeric(RPL_TAGS, "%s %s %s :has tags", user->nick.c_str(), target_user->nick.c_str(),
							   seralized.c_str());
		}

		return CMD_SUCCESS;
	}
};

class UserInfoModule : public Module
{
	bool onlyOpersSeeTags;
	UserInfoCommand cmd;

	bool MatchInfo(User* u, const std::string& mask)
	{
		irc::commasepstream stream(mask);
		std::string token;
		TagInfo ti = parseTagInfo(mask);
		UserInfo *info = cmd.ext.get_user(u);
		for (TagInfo::const_iterator it = ti.begin(), it_end = ti.end(); it != it_end; ++it)
			if ((info->count(it->first) != 0) != it->second)
				return false;

		return true;
	}

 public:
	UserInfoModule() : onlyOpersSeeTags(false), cmd(this)
	{
	}

	void init()
	{
		ServerInstance->Modules->AddService(cmd);
		ServerInstance->Modules->AddService(cmd.ext);
		Implementation eventlist[] = { I_OnCheckBan, I_OnWhois, I_On005Numeric };
		ServerInstance->Modules->Attach(eventlist, this, sizeof(eventlist)/sizeof(Implementation));
		OnRehash(NULL);
	}

	void OnRehash(User*)
	{
		ConfigTag* tag = ServerInstance->Config->ConfValue("userinfo");
		onlyOpersSeeTags = tag->getBool("operonly");
	}

	ModResult OnCheckBan(User* user, Channel* chan, const std::string& mask)
	{
		if (mask.length() > 2 && mask[0] == EXTBAN_CHAR && mask[1] == ':')
		{
			if (MatchInfo(user, mask.substr(2)))
				return MOD_RES_DENY;
		}
		return MOD_RES_PASSTHRU;
	}

	void OnWhois(User* user, User* dest)
	{
		if (onlyOpersSeeTags && !user->HasPrivPermission("users/auspex"))
			return;

		UserInfo* info = cmd.ext.get_user(dest);
		if (info->empty())
			return;

		std::string serealized = cmd.ext.serialize(FORMAT_USER, dest, info);
		ServerInstance->SendWhoisLine(user, dest, 310, "%s %s :has tags: %s", user->nick.c_str(), dest->nick.c_str(), serealized.c_str());
	}

	void On005Numeric(std::string &output)
	{
		ServerInstance->AddExtBanChar(EXTBAN_CHAR);
		output.append(" USERTAGS");
	}

	Version GetVersion()
	{
		return Version("Adds the ability for opers to associate certain 'tags' with a user.");
	}
};

MODULE_INIT(UserInfoModule)
