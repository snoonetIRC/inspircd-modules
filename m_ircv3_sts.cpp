/*
 * InspIRCd -- Internet Relay Chat Daemon
 *
 *   Copyright (C) 2015 Attila Molnar <attilamolnar@hush.com>
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
#include "modules/cap.h"

class STSCap : public Cap::Capability
{
	std::string policystr;

	bool OnRequest(LocalUser* user, bool adding) CXX11_OVERRIDE
	{
		return false;
	}

	const std::string* GetValue(LocalUser* user) const CXX11_OVERRIDE
	{
		return &policystr;
	}

 public:
	STSCap(Module* mod)
		: Cap::Capability(mod, "inspircd.org/sts")
	{
	}

	void SetPolicy(unsigned long duration, unsigned int port, bool preload)
	{
		std::string newpolicystr = "duration=";
		newpolicystr.append(ConvToStr(duration)).append(",port=").append(ConvToStr(port));
		if (preload)
			newpolicystr.append(",preload");
		if (policystr != newpolicystr)
		{
			ServerInstance->Logs->Log(MODNAME, LOG_DEBUG, "STS policy changed to \"%s\"", newpolicystr.c_str());
			policystr.swap(newpolicystr);
			NotifyValueChange();
		}
	}
};

class ModuleIRCv3STS : public Module
{
	STSCap cap;

 public:
	ModuleIRCv3STS()
		: cap(this)
	{
	}

	void ReadConfig(ConfigStatus& status) CXX11_OVERRIDE
	{
		ConfigTag* tag = ServerInstance->Config->ConfValue("sts");
		unsigned int port = tag->getInt("port", 6697);
		if ((port <= 0) || (port > 65535))
		{
			ServerInstance->Logs->Log(MODNAME, LOG_DEFAULT, "Invalid port specified (%u), not applying policy", port);
			return;
		}

		unsigned long duration = tag->getInt("duration", 60*60*24*30*2);
		bool preload = tag->getBool("preload");
		cap.SetPolicy(duration, port, preload);
	}

	Version GetVersion() CXX11_OVERRIDE
	{
		return Version("Strict Transport Security policy advertisement proof-of-concept");
	}
};

MODULE_INIT(ModuleIRCv3STS)
