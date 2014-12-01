/*
* Copyright (C) 2008-2010 TrinityCore <http://www.trinitycore.org/>
*
* This program is free software; you can redistribute it and/or modify it
* under the terms of the GNU General Public License as published by the
* Free Software Foundation; either version 2 of the License, or (at your
* option) any later version.
*
* This program is distributed in the hope that it will be useful, but WITHOUT
* ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
* FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
* more details.
*
* You should have received a copy of the GNU General Public License along
* with this program. If not, see <http://www.gnu.org/licenses/>.
*/

#include "ScriptPCH.h"
#include "ruby_sanctum.h"

class instance_ruby_sanctum : public InstanceMapScript
{
public:
    instance_ruby_sanctum() : InstanceMapScript("instance_ruby_sanctum", 724) { }

    InstanceScript* GetInstanceScript(InstanceMap* pMap) const
    {
        return new instance_ruby_sanctum_InstanceMapScript(pMap);
    }

    struct instance_ruby_sanctum_InstanceMapScript : public InstanceScript
    {
        instance_ruby_sanctum_InstanceMapScript(Map* pMap) : InstanceScript(pMap) {Initialize();};

        std::string strSaveData;

        //Creatures GUID
        uint32 m_auiEncounter[MAX_ENCOUNTERS+1];

        uint32 m_auiEventTimer;
        uint32 m_auiHalionEvent;

        uint32 m_auiOrbDirection;
        uint32 m_auiOrbNState;
        uint32 m_auiOrbSState;
        uint32 m_auiOrbIState;
        uint32 m_auiOrbWState;

        ObjectGuid m_uiHalion_pGUID;
        ObjectGuid m_uiHalion_tGUID;
        ObjectGuid m_uiHalionControlGUID;
        ObjectGuid m_uiRagefireGUID;
        ObjectGuid m_uiZarithianGUID;
        ObjectGuid m_uiBaltharusGUID;
        ObjectGuid m_uiCloneGUID;
        ObjectGuid m_uiXerestraszaGUID;

        ObjectGuid m_uiOrbNGUID;
        ObjectGuid m_uiOrbSGUID;
        ObjectGuid m_uiOrbIGUID;
        ObjectGuid m_uiOrbWGUID;
        ObjectGuid m_uiOrbFocusGUID;
        ObjectGuid m_uiOrbCarrierGUID;

        //object GUID
        ObjectGuid m_uiHalionPortal1GUID;
        ObjectGuid m_uiHalionPortal2GUID;
        ObjectGuid m_uiHalionPortal3GUID;
        ObjectGuid m_uiHalionFireWallSGUID;
        ObjectGuid m_uiHalionFireWallMGUID;
        ObjectGuid m_uiHalionFireWallLGUID;
        ObjectGuid m_uiBaltharusTargetGUID;

        ObjectGuid m_uiFireFieldGUID;
        ObjectGuid m_uiFlameWallsGUID;
        ObjectGuid m_uiFlameRingGUID;

        bool intro;

        void Initialize()
        {
            for (uint8 i = 0; i < MAX_ENCOUNTERS; ++i)
                m_auiEncounter[i] = NOT_STARTED;

            m_auiEventTimer = 1000;

            m_uiHalion_pGUID.Clear();
            m_uiHalion_tGUID.Clear();
            m_uiRagefireGUID.Clear();
            m_uiZarithianGUID.Clear();
            m_uiBaltharusGUID.Clear();
            m_uiCloneGUID.Clear();
            m_uiHalionPortal1GUID.Clear();
            m_uiHalionPortal2GUID.Clear();
            m_uiHalionPortal3GUID.Clear();
            m_uiXerestraszaGUID.Clear();
            m_uiHalionFireWallSGUID.Clear();
            m_uiHalionFireWallMGUID.Clear();
            m_uiHalionFireWallLGUID.Clear();
            m_uiBaltharusTargetGUID.Clear();
            m_auiOrbDirection = 0;
            m_uiOrbNGUID.Clear();
            m_uiOrbSGUID.Clear();
            m_uiOrbFocusGUID.Clear();
            m_auiOrbNState = NOT_STARTED;
            m_auiOrbSState = NOT_STARTED;
            m_auiOrbIState = NOT_STARTED;
            m_auiOrbWState = NOT_STARTED;
            intro = false;
        }

        bool IsEncounterInProgress() const
        {
            for(uint8 i = 1; i < MAX_ENCOUNTERS ; ++i)
                if (m_auiEncounter[i] == IN_PROGRESS)
                    return true;

            return false;
        }

        void OpenDoor(ObjectGuid const& guid)
        {
            if(!guid)
                return;

            GameObject* pGo = instance->GetGameObject(guid);
            if(pGo)
                pGo->SetGoState(GO_STATE_ACTIVE_ALTERNATIVE);
        }

        void CloseDoor(ObjectGuid const& guid)
        {
            if(!guid)
                return;

            GameObject* pGo = instance->GetGameObject(guid);
            if(pGo)
                pGo->SetGoState(GO_STATE_READY);
        }

        void OpenAllDoors()
        {
            if (m_auiEncounter[TYPE_RAGEFIRE] == DONE &&
                m_auiEncounter[TYPE_BALTHARUS] == DONE &&
                m_auiEncounter[TYPE_XERESTRASZA] == DONE)
                     OpenDoor(m_uiFlameWallsGUID);
            else
                CloseDoor(m_uiFlameWallsGUID);
        }

       
        void UpdateWorldState(bool command, uint32 value)
        {
           Map::PlayerList const &players = instance->GetPlayers();

           if (command)
           {
               for (Map::PlayerList::const_iterator i = players.begin(); i != players.end(); ++i)
               {
                   if (Player* pPlayer = i->getSource())
                   {
                        if (pPlayer->isAlive())
                        {
                            pPlayer->SendUpdateWorldState(UPDATE_STATE_UI_SHOW, 0);

                            if (pPlayer->HasAura(74807))
                                pPlayer->SendUpdateWorldState(UPDATE_STATE_UI_COUNT_R, 100 - value);
                            else 
								pPlayer->SendUpdateWorldState(UPDATE_STATE_UI_COUNT_T, value);

                            pPlayer->SendUpdateWorldState(UPDATE_STATE_UI_SHOW, 1);
                        }
                   }
               }
		   }
           else
           {
			   for (Map::PlayerList::const_iterator i = players.begin(); i != players.end(); ++i)
                  if(Player* pPlayer = i->getSource())
                        if(pPlayer->isAlive())
                            pPlayer->SendUpdateWorldState(UPDATE_STATE_UI_SHOW,0);
           }
        }

        void OnCreatureCreate(Creature* pCreature)
        {
            switch(pCreature->GetEntry())
            {
                case NPC_HALION_REAL: m_uiHalion_pGUID = pCreature->GetGUID();break;
	            case NPC_HALION_TWILIGHT: m_uiHalion_tGUID = pCreature->GetGUID(); break;
                case NPC_HALION_CONTROL: m_uiHalionControlGUID = pCreature->GetGUID(); break;
                case NPC_RAGEFIRE: m_uiRagefireGUID = pCreature->GetGUID(); break;
                case NPC_ZARITHIAN: m_uiZarithianGUID = pCreature->GetGUID(); break;
                case NPC_BALTHARUS: m_uiBaltharusGUID = pCreature->GetGUID(); break;
                case NPC_BALTHARUS_TARGET: m_uiBaltharusTargetGUID = pCreature->GetGUID(); break;
                case NPC_CLONE: m_uiCloneGUID = pCreature->GetGUID(); break;
                case NPC_XERESTRASZA: m_uiXerestraszaGUID = pCreature->GetGUID(); break;
                case NPC_SHADOW_PULSAR_N: m_uiOrbNGUID = pCreature->GetGUID(); break;
                case NPC_SHADOW_PULSAR_S: m_uiOrbSGUID = pCreature->GetGUID(); break;
                case NPC_SHADOW_PULSAR_I: m_uiOrbIGUID = pCreature->GetGUID(); break;
                case NPC_SHADOW_PULSAR_W: m_uiOrbWGUID = pCreature->GetGUID(); break;
                case NPC_ORB_ROTATION_FOCUS: m_uiOrbFocusGUID = pCreature->GetGUID(); break;
                case NPC_ORB_CARRIER: m_uiOrbCarrierGUID = pCreature->GetGUID(); break;
            }
        }

        void OnGameObjectCreate(GameObject* pGo)
        {
            switch(pGo->GetEntry())
            {
                case GO_HALION_PORTAL_1: m_uiHalionPortal1GUID = pGo->GetGUID(); break;
                case GO_HALION_PORTAL_2: m_uiHalionPortal2GUID = pGo->GetGUID(); break;
                case GO_HALION_PORTAL_3: m_uiHalionPortal3GUID = pGo->GetGUID(); break;
                case GO_FLAME_WALLS: m_uiFlameWallsGUID = pGo->GetGUID(); break;
                case GO_FLAME_RING: m_uiFlameRingGUID = pGo->GetGUID(); break;
                case GO_FIRE_FIELD: m_uiFireFieldGUID = pGo->GetGUID(); break;
            }
            OpenAllDoors();
        }

        void SetData(uint32 uiType, uint32 uiData)
        {
            switch(uiType)
            {
                case TYPE_EVENT: m_auiEncounter[uiType] = uiData; uiData = NOT_STARTED; break;
                case TYPE_RAGEFIRE:
                    m_auiEncounter[uiType] = uiData;
                    OpenAllDoors();
                    break;
                case TYPE_BALTHARUS: 
                    m_auiEncounter[uiType] = uiData;
                    OpenAllDoors();
                    break;
                case TYPE_XERESTRASZA: 
                    m_auiEncounter[uiType] = uiData;
                    if (uiData == IN_PROGRESS)
                        OpenDoor(m_uiFireFieldGUID);
                    else if (uiData == NOT_STARTED)
                    {
                        CloseDoor(m_uiFireFieldGUID);
                        OpenAllDoors();
                    }
                    else if (uiData == DONE)
                    {
                        OpenAllDoors();
                        if (m_auiEncounter[TYPE_ZARITHRIAN] == DONE)
                        {
                            m_auiEncounter[TYPE_EVENT] = 200;
                            m_auiEventTimer = 30000;
                        };
                    }
                    break;
                case TYPE_ZARITHRIAN:
                    m_auiEncounter[uiType] = uiData;
                    if (uiData == DONE)
                    {
                        OpenDoor(m_uiFlameWallsGUID);
                        m_auiEncounter[TYPE_EVENT] = 200;
                        m_auiEventTimer = 30000;
                    }
                    else if (uiData == IN_PROGRESS)
                        CloseDoor(m_uiFlameWallsGUID);
                    else if (uiData == FAIL)
                        OpenDoor(m_uiFlameWallsGUID);
                    break;
                case TYPE_HALION: m_auiEncounter[uiType] = uiData; break;
                case TYPE_HALION_EVENT: m_auiHalionEvent = uiData; uiData = NOT_STARTED; break;
                case TYPE_EVENT_TIMER: m_auiEventTimer = uiData; uiData = NOT_STARTED; break;

                case DATA_ORB_DIRECTION: m_auiOrbDirection = uiData; uiData = NOT_STARTED; break;
                case DATA_ORB_N: m_auiOrbNState = uiData; uiData = NOT_STARTED; break;
                case DATA_ORB_S: m_auiOrbSState = uiData; uiData = NOT_STARTED; break;
                case DATA_ORB_I: m_auiOrbIState = uiData; uiData = NOT_STARTED; break;
                case DATA_ORB_W: m_auiOrbWState = uiData; uiData = NOT_STARTED; break;
                case TYPE_COUNTER:
                    {
                        if (uiData == 255)
                            UpdateWorldState(false, uiData);
                        else
                            UpdateWorldState(true,uiData);
                        uiData = NOT_STARTED;
                        break;
                    }
            }

            if (uiData == DONE)
            {
                if (InstanceMap* im = instance->ToInstanceMap())
                {
                    InstanceScript* pInstance = im->GetInstanceScript();
                    uint8 bossdata = 0;

                    for (uint32 data = TYPE_RAGEFIRE; data <= TYPE_ZARITHRIAN; data++)
                    {
                        if (pInstance->GetData(data) == DONE)
                            bossdata++;
                        else 
                            break;
                    }
                    
                    if (bossdata == 4 && !intro)
                    {
                        if (Creature* Halion = instance->GetCreature(m_uiHalion_pGUID))
                        {
                            if (Halion && Halion->isAlive())
                            {
                                intro = true;
                                Halion->AI()->DoAction(ACTION_INTRO_HALION);
                            }
                        }
                    }
                }
            }

            if (uiData == DONE)
            {
                OUT_SAVE_INST_DATA;

                std::ostringstream saveStream;

                for(uint8 i = 0; i < MAX_ENCOUNTERS; ++i)
                    saveStream << m_auiEncounter[i] << " ";

                strSaveData = saveStream.str();

                SaveToDB();
                OUT_SAVE_INST_DATA_COMPLETE;
            }
        }

        std::string GetSaveData()
        {
            return strSaveData;
        }

        uint32 GetData(uint32 uiType)
        {
            switch(uiType)
            {
                case TYPE_RAGEFIRE: return m_auiEncounter[uiType];
                case TYPE_BALTHARUS: return m_auiEncounter[uiType];
                case TYPE_XERESTRASZA: return m_auiEncounter[uiType];
                case TYPE_ZARITHRIAN: return m_auiEncounter[uiType];
                case TYPE_HALION: return m_auiEncounter[uiType];

                case TYPE_EVENT: return m_auiEncounter[uiType];

                case TYPE_HALION_EVENT: return m_auiHalionEvent;

                case TYPE_EVENT_TIMER: return m_auiEventTimer;
                case TYPE_EVENT_NPC: switch (m_auiEncounter[TYPE_EVENT])
                                         {
                                              case 10:
                                              case 20:
                                              case 30:
                                              case 40:
                                              case 50:
                                              case 60:
                                              case 70:
                                              case 80:
                                              case 90:
                                              case 100:
                                              case 110:
                                              case 200:
                                              case 210:
                                                     return NPC_XERESTRASZA;
                                                     break;
                                              default:
                                                     break;
                                         };
                                         return 0;

                case DATA_ORB_DIRECTION: return m_auiOrbDirection;
                case DATA_ORB_N: return m_auiOrbNState;
                case DATA_ORB_S: return m_auiOrbSState;
                case DATA_ORB_I: return m_auiOrbIState;
                case DATA_ORB_W: return m_auiOrbWState;

            }
            return 0;
        }

        ObjectGuid GetGuidData(uint32 uiData)
        {
            switch(uiData)
            {
                case NPC_BALTHARUS: return m_uiBaltharusGUID;
                case NPC_CLONE: return m_uiCloneGUID;
                case NPC_ZARITHIAN: return m_uiZarithianGUID;
                case NPC_RAGEFIRE: return m_uiRagefireGUID;
                case NPC_HALION_REAL: return m_uiHalion_pGUID;
                case NPC_HALION_TWILIGHT: return m_uiHalion_tGUID;
                case NPC_HALION_CONTROL: return m_uiHalionControlGUID;
                case NPC_XERESTRASZA: return m_uiXerestraszaGUID;
                case NPC_BALTHARUS_TARGET: return m_uiBaltharusTargetGUID;

                case GO_FLAME_WALLS: return m_uiFlameWallsGUID;
                case GO_FLAME_RING: return m_uiFlameRingGUID;
                case GO_FIRE_FIELD: return m_uiFireFieldGUID;

                case GO_HALION_PORTAL_1: return m_uiHalionPortal1GUID;
                case GO_HALION_PORTAL_2: return m_uiHalionPortal2GUID;
                case GO_HALION_PORTAL_3: return m_uiHalionPortal3GUID;

                case NPC_SHADOW_PULSAR_N: return m_uiOrbNGUID;
                case NPC_SHADOW_PULSAR_S: return m_uiOrbSGUID;
                case NPC_SHADOW_PULSAR_I: return m_uiOrbIGUID;
                case NPC_SHADOW_PULSAR_W: return m_uiOrbWGUID;
                case NPC_ORB_ROTATION_FOCUS: return m_uiOrbFocusGUID;
                case NPC_ORB_CARRIER: return m_uiOrbCarrierGUID;
            }
            return ObjectGuid::Empty;
        }

        void Load(const char* chrIn)
        {
            if (!chrIn)
            {
                OUT_LOAD_INST_DATA_FAIL;
                return;
            }

            OUT_LOAD_INST_DATA(chrIn);

            std::istringstream loadStream(chrIn);

            for(uint8 i = 0; i < MAX_ENCOUNTERS; ++i)
            {
                loadStream >> m_auiEncounter[i];

                if (m_auiEncounter[i] == IN_PROGRESS
                    || m_auiEncounter[i] == FAIL)
                    m_auiEncounter[i] = NOT_STARTED;
            }

            m_auiEncounter[TYPE_XERESTRASZA] = NOT_STARTED;

            OUT_LOAD_INST_DATA_COMPLETE;
            OpenAllDoors();
        }
    };
};


void AddSC_instance_ruby_sanctum()
{
    new instance_ruby_sanctum;
}
