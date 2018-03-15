/* $ModDesc: Provides inter-server REMOTEUSER */
/* $ModAuthor: Katrina Swales */
/* $ModAuthorMail: kat.swales@nekokittygames.com */
/* $ModDepends: core 2.0 */

#include "inspircd.h"
#include "xline.h"

//#define DEBUG_REMOTEUSER

/* Removes any ! characters from a given nick */
static std::string strip_npc_nick(const std::string &nick)
{
	std::string newnick;
	unsigned len = nick.size();
	for (unsigned x = 0; x < len; ++x)
	{
		char c = nick[x];
		if (c != '!')
			newnick += c;
	}
	return newnick;
}

/* Sends a message to a channel, splitting it as needed if the message is too long.
 * It'll usually only split into 2 messages because of the 512 character limit. */
static void send_message(Channel *c, const std::string &source, std::string text, bool action)
{
	/* 510 - colon prefixing source - PRIVMSG - colon prefixing text - 3 spaces = 498
	 * Subtracting the source and the channel name to get how many characters we are allowed left
	 * If doing an action, subtract an additional 9 for the startind and ending ASCII character 1, ACTION and space */
	int allowedMessageLength = 498 - source.size() - c->name.size() - (action ? 9 : 0);
	/* This will keep attempting to determine if there is text to send */
	do
	{
		std::string textToSend = text;
		/* Check if the current text to send exceeds the length allowed */
		if (textToSend.size() > allowedMessageLength)
		{
			/* Look for the last space at or before the length allowed */
			size_t lastSpace = textToSend.find_last_of(' ', allowedMessageLength);
			/* If a space was found, split off the text to send */
			if (lastSpace != std::string::npos)
			{
				textToSend = text.substr(0, lastSpace);
				text = text.substr(lastSpace + 1);
			}
			/* Otherwise, we'll send whatever we have left, even if it may be too long */
			else
				text.clear();
		}
		else
			text.clear();

		c->WriteChannelWithServ(source, "PRIVMSG %s :%s%s%s", c->name.c_str(), action ? "\1ACTION " : "", textToSend.c_str(), action ? "\1" : "");
	} while (!text.empty());
}

/*
 * NOTE: For all commands, the user in the Handle function is checked to be local or not.
 *
 * If they are local, then the command passed through the module's OnPreCommand and the
 * text was set accordingly to prevent colon eating from happening. Channel is checked,
 * and if valid, user status for being an op in the channel is checked. Assuming all that
 * succeeds, then the command is set to the channel locally and then broadcast via ENCAP.
 * The reason that the ENCAP is created manually instead of automatically through the
 * GetRouting function is to prevent the same colon eating issue we handle in OnPreCommand.
 *
 * If they are not local, then the command must've come remotely and thus is being sent
 * directly to the handler. No channel or user checks are done, as they are assumed to
 * have been valid on the originating server, but the text was passed via ENCAP in such
 * a way that colon eating is not an issue and the text must be set, otherwise the
 * previous text from a local usage will be used. Broadcasting is skipped, as it would
 * be pretty bad to broadcast infinitely.
 */

/** Base class for /NPC and /NPCA
 */
class NPCx
{
	std::string cmdName, text;

public:
	NPCx(const std::string &cmd) : cmdName(cmd)
	{
	}

	CmdResult Handle(const std::vector<std::string> &parameters, User *user, bool action)
	{
		Channel *c = ServerInstance->FindChan(parameters[0]);
        LocalUser *localUser = IS_LOCAL(user);
#ifdef DEBUG_REMOTEUSER
		
		if (localUser)
		{
			if (c)
			{
				if (!c->HasUser(user))
				{
					user->WriteNumeric(ERR_NOTONCHANNEL, "%s %s :You are not on that channel!", user->nick.c_str(), parameters[0].c_str());
					return CMD_FAILURE;
				}
			}
			else
			{
				user->WriteNumeric(ERR_NOSUCHCHANNEL, "%s %s :No such channel", user->nick.c_str(), parameters[0].c_str());
				return CMD_FAILURE;
			}
		}
		else
		{
#else
        	if(!localUser)
		{
#endif
			this->text = parameters[2];

		/* Source is in the form of: [nick]!npc@[server-name] */
		std::string npc_nick = strip_npc_nick(parameters[1]);
		std::string npc_source = npc_nick + "!npc@" + ServerInstance->Config->ServerName;

			send_message(c, npc_source, this->text, action);
#ifdef DEBUG_REMOTEUSER
			if (localUser)
			{
				std::vector<std::string> params;
				params.push_back("*");
				params.push_back(this->cmdName);
				params.push_back(parameters[0]);
				params.push_back(npc_nick);
				params.push_back(":" + this->text);
				ServerInstance->PI->SendEncapsulatedData(params);
			}
#endif
		}
		return CMD_SUCCESS;
	}

	void SetText(const std::string &newText)
	{
		this->text = newText;
	}
};

/** Handle /REMOTEUSER
 */
class CommandRemoteUser : public Command, public NPCx
{
public:
	CommandRemoteUser(Module *parent) : Command(parent, "REMOTEUSER", 3, 3), NPCx("REMOTEUSER")
	{
		this->syntax = "<channel> <name> <text>";
	}

	CmdResult Handle(const std::vector<std::string> &parameters, User *user)
	{
		return NPCx::Handle(parameters, user, false);
	}
};




class ModuleRemoteUserCommand : public Module
{
	CommandRemoteUser remote_user;

public:
	ModuleRemoteUserCommand() : remote_user(this)
	{
    }

	~ModuleRemoteUserCommand()
	{
		
	}

	void init()
	{
		ServiceProvider *services[] = { &this->remote_user, };
		ServerInstance->Modules->AddServices(services, sizeof(services) / sizeof(services[0]));

		ServerInstance->Modules->Attach(I_OnPreCommand, this);
	}

	Version GetVersion()
	{
		return Version("Provides REMOTEUSER", VF_COMMON);
	}

	/** The purpose of this is to make it so the command text doesn't require a colon prefixing the text but also to allow a colon to start a word anywhere in the line.
	 */
	ModResult OnPreCommand(std::string &command, std::vector<std::string> &parameters, LocalUser *user, bool validated, const std::string &original_line)
	{
		irc::spacesepstream sep(original_line);
		std::string text;
		sep.GetToken(text);
		sep.GetToken(text);
		if (command == "REMOTEUSER")
		{
			sep.GetToken(text);
			text = sep.GetRemaining();
				this->remote_user.SetText(text);
		}
		return MOD_RES_PASSTHRU;
	}
};

MODULE_INIT(ModuleRemoteUserCommand)
