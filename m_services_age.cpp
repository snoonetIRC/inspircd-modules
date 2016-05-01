/*
 * InspIRCd -- Internet Relay Chat Daemon
 *
 *   Copyright (C) 2012 Shawn Smith <shawn@inspircd.org>
 *   Copyright (C) 2009 Daniel De Graaf <danieldg@inspircd.org>
 *   Copyright (C) 2006-2008 Robin Burchell <robin+git@viroteck.net>
 *   Copyright (C) 2008 Pippijn van Steenhoven <pip88nl@gmail.com>
 *   Copyright (C) 2006, 2008 Craig Edwards <craigedwards@brainbox.cc>
 *   Copyright (C) 2007 Dennis Friis <peavey@inspircd.org>
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


/* $ModDesc: Provides support for ircu-style services accounts, including chmode +R, etc. */

#include "inspircd.h"




/** Channel mode +R - unidentified users cannot join

class AChannel_R : public SimpleChannelModeHandler
{
 public:
	AChannel_R(Module* Creator) : SimpleChannelModeHandler(Creator, "reginvite", 'R') { }
};
 */

class User_V : public ModeHandler
{
    
public:
    User_V(Module* Creator) : ModeHandler(Creator, "u_user_age", 'V', PARAM_ALWAYS, MODETYPE_USER) { }
    
    ModeAction OnModeChange(User* source, User* dest, Channel* channel, std::string &parameter, bool adding)
    {
        //if (!IS_LOCAL(source))
        //{
        if(adding)
        {
            dest->SetMode('V',true);
            GetAccountAgeExtItem()->set(dest,parameter);
            return MODEACTION_ALLOW;
        }
        
        //}
        //else
        //{
        //	source->WriteNumeric(500, "%s :Only a server may modify the +V user mode", source->nick.c_str());
        //}
        //return MODEACTION_DENY;
    }
    std::string GetUserParameter(User* useor)
    {
        return *(GetAccountAgeExtItem()->get(useor));
    }
    int GetNumParams(bool adding)
    {
        return 1;
    }
    void OnParameterMissing(User* user, User* dest, Channel* channel)
    {
        user->WriteServ("NOTICE %s :*** The user mode +V requires a parameter. Please provide a parameter, e.g. '+V *'.",
                        user->nick.c_str());
    }
};

class ModuleServicesAge : public Module

{
	//AChannel_R m1;
	User_V m5;
	AccountAgeExtItem accountage;
    
	bool checking_ban;


 public:
	ModuleServicesAge() : m5(this),
		accountage("accountage", this), checking_ban(false)
	{
	}

	void init()
	{
		ServiceProvider* providerlist[] = {   &m5, &accountage };
		ServerInstance->Modules->AddServices(providerlist, sizeof(providerlist)/sizeof(ServiceProvider*));
        Implementation eventlist[] = { I_OnCheckBan,I_OnWhoisLine, I_On005Numeric };
        ServerInstance->Modules->Attach(eventlist, this, sizeof(eventlist)/sizeof(Implementation));

        
	}
    AccountAgeExtItem* GetAccountAgeExtItem() { return &accountage; }

    void On005Numeric(std::string &t)
    {
        ServerInstance->AddExtBanChar('V');
    }
    
    // :kenny.chatspike.net 320 Brain Azhrarn :is getting paid to play games.
    ModResult OnWhoisLine(User* user, User* dest, int &numeric, std::string &text)
    {
        /* We use this and not OnWhois because this triggers for remote, too */
        if (numeric == 312)
        {
            std::string *acctage = accountage.get(dest);
            
            if (acctage)
            {
                whois.SendLine(320, ": is at least "+acctage+" days old ");
                ServerInstance->SendWhoisLine(user, dest, 320, "%s %s :%s",user->nick.c_str(), dest->nick.c_str(), sprintf();
            }
        }
        
        /* Dont block anything */
        return MOD_RES_PASSTHRU;
    }
    
    ModResult OnCheckBan(User* user, Channel* chan, const std::string& mask)
    {
        if ((mask.length() > 2) && (mask[1] == ':'))
        {
            if (mask[0] == 'V')
            {
                std::string *accage = accountage.get(user);
                if(accage)
                {
                    int age=atoi(accage->c_str());
                    int ban=atoi(mask.substr(2).c_str());
                    if(age <= ban)
                        return MOD_RES_DENY;
                }
            }
        }
        
        /* If we made it this far then the ban wasn't an ExtBan
         or the user we were checking for didn't match either ExtBan */
        return MOD_RES_PASSTHRU;
    }

	Version GetVersion()
	{
		return Version("Provides support for ircu-style services accounts, including umode +V, etc.",VF_OPTCOMMON|VF_VENDOR);
	}
};

MODULE_INIT(ModuleServicesAge)
