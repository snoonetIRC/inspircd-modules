/*
 * Gathers metrics to send to a local telegraf instance, connecting on a configurable port
 *
 * Flow:
 * 		Module init
 * 			Create Timer
 * 			Create AtomicAction
 * 			Register Timer
 *
 * 		From Loop:
 * 			LoopLagTimer::Tick
 * 			OnBackgroundtimer (~every 5 secs)
 * 			Socket reads/module calls
 * 			LoopAction::Call
 *
 * 	Data fields can be added in TelegrafSocket::SendMetrics
 *
 * 	Config:
 * 		<module name="m_telegraf.so">
 * 		<telegraf
 * 			# Port Telegraf is listening on
 * 			port="8094"
 * 			Whether to announce the start and stop of metrics with a snotice
 * 			silent="false"
 * 			How often to attempt to reconnect to Telegraf after losing connection
 * 			reconnect="60">
 */

#include "inspircd.h"
#include "commands/cmd_whowas.h"

static const std::string cmd_actions[] = {"start", "stop", "restart", "status", "sample"};

struct Metrics
{
	typedef std::vector<std::clock_t> LoopTimes;
	std::clock_t lastLoopTime;
	LoopTimes loopTimes;

	Metrics() : lastLoopTime(0)
	{
	}

	virtual ~Metrics()
	{
	}

	void clear()
	{
		loopTimes.clear();
		lastLoopTime = 0;
	}

	void addLoopTime(const std::clock_t t)
	{
		loopTimes.push_back(t - lastLoopTime);
	}

	std::clock_t getAverageLoopTime()
	{
		if (loopTimes.empty())
			return 0;
		std::clock_t total = 0;
		for (LoopTimes::size_type i = 0; i < loopTimes.size(); ++i)
			total += loopTimes[i];
		return total / loopTimes.size();
	}
};

struct TelegrafLine
{
	std::string name;
	std::map<std::string, std::string> tags;
	std::map<std::string, std::string> fields;

	TelegrafLine()
	{
	}

	virtual ~TelegrafLine()
	{
	}

	void clear()
	{
		name.clear();
		tags.clear();
		fields.clear();
	}

	std::string escapeTag(const std::string &in)
	{
		std::string out;
		for (std::string::const_iterator i = in.begin(); i != in.end(); i++)
		{
			char c = *i;
			switch (c)
			{
				case ',':
				case ' ':
				case '=':
				case '\\':
					out += '\\';
				default:
					out += c;
			}
		}
		return out;
	}

	std::string escapeValue(const std::string &in)
	{
		std::string out;
		for (std::string::const_iterator i = in.begin(); i != in.end(); i++)
		{
			switch (*i)
			{
				case '"':
				case '\\':
					out += '\\';
				default:
					out += *i;
			}
		}
		return out;
	}

	std::string format()
	{
		std::string out(name);
		for (std::map<std::string, std::string>::const_iterator i = tags.begin(); i != tags.end(); i++)
		{
			out += "," + escapeTag(i->first) + "=" + escapeTag(i->second);
		}
		bool first = true;
		for (std::map<std::string, std::string>::const_iterator i = fields.begin(); i != fields.end(); i++)
		{
			if (first)
			{
				out += " ";
				first = false;
			}
			else
			{
				out += ",";
			}
			out += escapeTag(i->first) + "=" + escapeValue(i->second);
		}
		return out + "\n";
	}
};

class TelegrafModule;

struct LoopAction : public HandlerBase0<void>
{
	TelegrafModule *creator;

	LoopAction(TelegrafModule *m) : creator(m)
	{
	}

	void Call();
};

struct LoopLagTimer : public Timer
{
	TelegrafModule *creator;

	LoopLagTimer(TelegrafModule *m) : Timer(0, 0, true), creator(m)
	{
	}

	void Tick(time_t);
};

class TelegrafSocket : public BufferedSocket
{
	TelegrafModule *creator;

 public:
	TelegrafSocket(TelegrafModule *m, int port) : creator(m)
	{
		DoConnect("127.0.0.1", port, 60, "");
	}

	void OnError(BufferedSocketError);

	void OnDataReady()
	{
		recvq.clear();
	}

	void SendMetrics();

	TelegrafLine GetMetrics();
};

class TelegrafCommand : public Command
{
	std::set<std::string> actions;

 public:
	TelegrafCommand(Module *parent)
			: Command(parent, "TELEGRAF", 1),
			  actions(cmd_actions, cmd_actions + sizeof(cmd_actions) / sizeof(cmd_actions[0]))
	{
		syntax = "{start|stop|restart|status} [<servername>]";
		flags_needed = 'o';
	}

	RouteDescriptor GetRouting(User *user, const std::vector<std::string> &parameters)
	{
		if (parameters.size() > 1)
			return ROUTE_BROADCAST;
		return ROUTE_LOCALONLY;
	}

	CmdResult Handle(const std::vector<std::string> &parameters, User *user);
};

class TelegrafModule : public Module
{
 public:
	Metrics metrics;

 private:
	bool shouldReconnect;
	bool silent;
	int port;
	long reconnectTimeout;
	time_t lastReconnect;
	LoopLagTimer *timer;
	LoopAction *action;
	TelegrafSocket *tSock;
	TelegrafCommand cmd;

	friend class TelegrafCommand;

 public:
	TelegrafModule()
			: shouldReconnect(false), silent(false), port(0), reconnectTimeout(0), lastReconnect(0), timer(NULL),
			  action(NULL), tSock(NULL), cmd(this)
	{
	}

	void init()
	{
		timer = new LoopLagTimer(this);
		action = new LoopAction(this);
		ServerInstance->Timers->AddTimer(timer);
		ServerInstance->Modules->AddService(cmd);
		Implementation eventlist[] = {I_OnRehash, I_OnBackgroundTimer};
		ServerInstance->Modules->Attach(eventlist, this, sizeof(eventlist) / sizeof(Implementation));
		OnRehash(NULL);
	}

	void OnRehash(User *user)
	{
		ConfigTag *tag = ServerInstance->Config->ConfValue("telegraf");
		silent = tag->getBool("silent");
		reconnectTimeout = tag->getInt("reconnect", 60);
		int new_port = tag->getInt("port");
		if (port != new_port)
		{
			if (tSock)
			{
				StopMetrics();
			}
			port = new_port;
			if (port > 0 && port < 65536)
			{
				StartMetrics();
			}
		}
	}

	void OnBackgroundTimer(time_t curtime)
	{
		if (shouldReconnect && !tSock)
		{
			if ((curtime - lastReconnect) >= reconnectTimeout)
			{
				lastReconnect = curtime;
				shouldReconnect = false;
				StartMetrics(true);
			}
		}
		else if ((tSock) && (tSock->GetFd() > -1))
		{
			tSock->SendMetrics();
		}
	}

	void LoopTick(bool first)
	{
		if (!tSock)
			return;

		if (first)
		{
			// Triggered from the timer
			metrics.lastLoopTime = std::clock();
			ServerInstance->AtomicActions.AddAction(action);
		}
		else if (metrics.lastLoopTime)
		{
			// Triggered from the atomic call
			metrics.addLoopTime(std::clock());
		}
	}

	void StartMetrics(bool restarted = false)
	{
		tSock = new TelegrafSocket(this, port);
		if (!silent)
			ServerInstance->SNO->WriteGlobalSno('a', "METRICS: Telegraf metrics %sstarted.", restarted ? "re" : "");
	}

	void StopMetrics(bool error = false)
	{
		ServerInstance->GlobalCulls.AddItem(tSock);
		if (!silent)
		{
			if (!error)
			{
				ServerInstance->SNO->WriteGlobalSno('a', "METRICS: Telegraf metrics stopped.");
			}
			else
			{
				ServerInstance->SNO->WriteGlobalSno('a', "METRICS: Socket error occurred: %s",
													tSock->getError().c_str());
			}
		}
		tSock = NULL;
		metrics.clear();
	}

	void SocketError(BufferedSocketError e)
	{
		StopMetrics(true);
		if (reconnectTimeout)
			shouldReconnect = true;
	}

	CullResult cull()
	{
		if (action)
			ServerInstance->GlobalCulls.AddItem(action);
		if (timer)
			ServerInstance->Timers->DelTimer(timer);
		if (tSock)
			StopMetrics();
		return Module::cull();
	}

	Version GetVersion()
	{
		return Version("Reports ircd stats to a locally running Telegraf instance", VF_COMMON);
	}
};

void LoopLagTimer::Tick(time_t)
{
	creator->LoopTick(true);
}

void LoopAction::Call()
{
	creator->LoopTick(false);
}

CmdResult TelegrafCommand::Handle(const std::vector<std::string> &parameters, User *user)
{
	if (actions.find(parameters[0]) == actions.end())
	{
		if (IS_LOCAL(user))
		{
			user->WriteNumeric(RPL_SYNTAX, "%s :SYNTAX %s %s", user->nick.c_str(), name.c_str(), syntax.c_str());
		}
		return CMD_FAILURE;
	}

	if (parameters.size() > 1 && !InspIRCd::Match(ServerInstance->Config->ServerName, parameters[1]))
	{
		// Route the command only to the remote server specified
		return CMD_SUCCESS;
	}

	TelegrafModule *mod = static_cast<TelegrafModule *> (static_cast<Module *> (creator));
	std::vector<std::string> messages;
	std::string message;
	if (parameters[0] == "start")
	{
		if (!mod->tSock)
		{
			mod->StartMetrics();
			messages.push_back("Telegraf metrics started");
		}
		else
		{
			messages.push_back("Telegraf metrics already running");
		}
	}
	else if (parameters[0] == "stop")
	{
		if (mod->tSock)
		{
			mod->shouldReconnect = false;
			mod->StopMetrics();
			messages.push_back("Telegraf metrics stopped");
		}
		else
		{
			messages.push_back("Telegraf metrics not running");
		}
	}
	else if (parameters[0] == "restart")
	{
		if (mod->tSock)
		{
			mod->StopMetrics();
			mod->StartMetrics(true);
			messages.push_back("Telegraf metrics restarted");
		}
		else
		{
			messages.push_back("Telegraf metrics not running");
		}
	}
	else if (parameters[0] == "status")
	{
		if (mod->tSock)
		{
			messages.push_back("Telegraf metrics running");
		}
		else
		{
			messages.push_back("Telegraf metrics not running");
		}
	}
	else if (parameters[0] == "sample")
	{
		if (mod->tSock)
		{
			TelegrafLine line = mod->tSock->GetMetrics();
			messages.push_back("Name: " + line.name);
			messages.push_back("Tags:");
			for (std::map<std::string, std::string>::const_iterator i = line.tags.begin(); i != line.tags.end(); ++i)
			{
				messages.push_back("    " + i->first + "=" + i->second);
			}
			messages.push_back("Values:");
			for (std::map<std::string, std::string>::const_iterator i = line.fields.begin();
				 i != line.fields.end(); ++i)
			{
				messages.push_back("    " + i->first + "=" + i->second);
			}
			messages.push_back("End of metrics");
		}
		else
		{
			messages.push_back("Telegraf metrics don't appear to be running");
		}
	}
	else
	{
		return CMD_FAILURE;
	}

	for (std::vector<std::string>::size_type i = 0; i < messages.size(); ++i)
	{
		if (parameters.size() > 1)
			user->SendText(":%s NOTICE %s :*** From %s: %s", ServerInstance->Config->ServerName.c_str(),
						   user->nick.c_str(), ServerInstance->Config->ServerName.c_str(), messages[i].c_str());
		else
			user->SendText(":%s NOTICE %s :*** %s", ServerInstance->Config->ServerName.c_str(), user->nick.c_str(),
						   messages[i].c_str());
	}

	return CMD_SUCCESS;
}

void TelegrafSocket::OnError(BufferedSocketError e)
{
	if (creator)
		creator->SocketError(e);
}

void TelegrafSocket::SendMetrics()
{
	ServerInstance->Logs->Log("TELEGRAF", DEBUG, "Sending Telegraf Metrics..");
	TelegrafLine line = GetMetrics();
	creator->metrics.loopTimes.clear();
	creator->metrics.loopTimes.reserve(10);
	std::string out(line.format());
	WriteData(out);
	ServerInstance->Logs->Log("TELEGRAF", DEBUG, "Sent Telegraf metrics: %s", out.c_str());
}

TelegrafLine TelegrafSocket::GetMetrics()
{
	TelegrafLine line;
	line.name = "ircd";
	line.tags["server"] = ServerInstance->Config->ServerName;
	line.fields["users"] = ConvToStr(ServerInstance->Users->LocalUserCount());
	float bits_in, bits_out, bits_total;
	ServerInstance->SE->GetStats(bits_in, bits_out, bits_total);
	line.fields["rate_in"] = ConvToStr(bits_in);
	line.fields["rate_out"] = ConvToStr(bits_out);
	line.fields["rate_total"] = ConvToStr(bits_total);
	if (ServerInstance->Config->WhoWasGroupSize && ServerInstance->Config->WhoWasMaxGroups)
	{
		Module *whowas = ServerInstance->Modules->Find("cmd_whowas.so");
		if (whowas)
		{
			WhowasRequest req(NULL, whowas, WhowasRequest::WHOWAS_STATS);
			req.user = ServerInstance->FakeClient;
			req.Send();
			// "Whowas entries: <size> (<size> bytes)"
			std::string stats = req.value;
			stats.erase(0, 16);
			// "<size> (<size> bytes)"
			std::string::size_type pos = stats.find_first_of(' ');
			// "<size> (<size> bytes)"
			//        ^
			line.fields["whowas_size"] = stats.substr(0, pos);
			stats.erase(0, pos + 2);
			// "<size> bytes)"
			pos = stats.find_first_of(' ');
			// "<size> bytes)"
			//        ^
			line.fields["whowas_bytes"] = stats.substr(0, pos);
		}
	}
	line.fields["data_sent"] = ConvToStr(ServerInstance->stats->statsSent);
	line.fields["data_recv"] = ConvToStr(ServerInstance->stats->statsRecv);
	line.fields["dns"] = ConvToStr(ServerInstance->stats->statsDns);
	line.fields["dns_good"] = ConvToStr(ServerInstance->stats->statsDnsGood);
	line.fields["dns_bad"] = ConvToStr(ServerInstance->stats->statsDnsBad);
	line.fields["sock_accepts"] = ConvToStr(ServerInstance->stats->statsAccept);
	line.fields["sock_refused"] = ConvToStr(ServerInstance->stats->statsRefused);
	line.fields["connects"] = ConvToStr(ServerInstance->stats->statsConnects);
	line.fields["nick_collisions"] = ConvToStr(ServerInstance->stats->statsCollisions);
	line.fields["cmd_unknown"] = ConvToStr(ServerInstance->stats->statsUnknown);
	line.fields["sockets"] = ConvToStr(ServerInstance->SE->GetUsedFds());
	line.fields["main_loop_time"] = ConvToStr(creator->metrics.getAverageLoopTime());
	return line;
}

MODULE_INIT(TelegrafModule)
