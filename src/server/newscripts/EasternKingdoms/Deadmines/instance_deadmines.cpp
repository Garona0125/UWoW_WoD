#include "NewScriptPCH.h"
#include "deadmines.h"

#define MAX_ENCOUNTER 6

static const DoorData doordata[] = 
{
    {GO_FACTORY_DOOR,   DATA_GLUBTOK,   DOOR_TYPE_PASSAGE,    BOUNDARY_NONE},
    {GO_MAST_ROOM_DOOR, DATA_HELIX,     DOOR_TYPE_PASSAGE,    BOUNDARY_NONE},
    {GO_FOUNDRY_DOOR,   DATA_FOEREAPER, DOOR_TYPE_PASSAGE,    BOUNDARY_NONE},
    {0, 0, DOOR_TYPE_ROOM, BOUNDARY_NONE},
};

class instance_deadmines : public InstanceMapScript
{
    public:
        instance_deadmines() : InstanceMapScript("instance_deadmines", 36) {}
		
        InstanceScript* GetInstanceScript(InstanceMap* pMap) const
        {
            return new instance_deadmines_InstanceMapScript(pMap);
        }

        struct instance_deadmines_InstanceMapScript : public InstanceScript
        {
			instance_deadmines_InstanceMapScript(Map* pMap) : InstanceScript(pMap) 
			{
				SetBossNumber(MAX_ENCOUNTER);
                LoadDoorData(doordata);
                
                uiGlubtokGUID.Clear();
                uiHelixGUID.Clear();
                uiOafGUID.Clear();
                uiFoereaperGUID.Clear();
                uiAdmiralGUID.Clear();
                uiCaptainGUID.Clear();

                IronCladDoorGUID.Clear();
                DefiasCannonGUID.Clear();
                DoorLeverGUID.Clear();

                State = CANNON_NOT_USED;
			};

			void OnCreatureCreate(Creature *pCreature)
			{
				switch (pCreature->GetEntry())
				{
				    case NPC_GLUBTOK:
					    uiGlubtokGUID = pCreature->GetGUID();
                        break;
				    case NPC_HELIX:
                        uiHelixGUID = pCreature->GetGUID();
                        break;
				    case NPC_OAF:
                        uiOafGUID = pCreature->GetGUID();
                        break;
				    case NPC_FOEREAPER:
                        uiFoereaperGUID = pCreature->GetGUID();
                        break;
				    case NPC_ADMIRAL:
				       uiAdmiralGUID = pCreature->GetGUID();
				       break;
				    case NPC_CAPTAIN:
				       uiCaptainGUID = pCreature->GetGUID();				
				       break;
				}
			}

			void OnGameObjectCreate(GameObject* pGo)
			{
				switch(pGo->GetEntry())
				{
				    case GO_FACTORY_DOOR:   
					    break;
				    case GO_MAST_ROOM_DOOR:   
				    case GO_FOUNDRY_DOOR:   
                        AddDoor(pGo, true);
					    break;
				    case GO_IRONCLAD_DOOR:  IronCladDoorGUID = pGo->GetGUID();  break;
				    case GO_DEFIAS_CANNON:  DefiasCannonGUID = pGo->GetGUID();  break;
				    case GO_DOOR_LEVER:     DoorLeverGUID = pGo->GetGUID();     break;
				}
			}

			void ShootCannon()
            {
                if (GameObject *pDefiasCannon = instance->GetGameObject(DefiasCannonGUID))
                {
                    pDefiasCannon->SetGoState(GO_STATE_ACTIVE);
                    DoPlaySound(pDefiasCannon, SOUND_CANNONFIRE);
                }
            }

            void BlastOutDoor()
            {
                if (GameObject *pIronCladDoor = instance->GetGameObject(IronCladDoorGUID))
                {
                    pIronCladDoor->SetGoState(GO_STATE_ACTIVE_ALTERNATIVE);
                    DoPlaySound(pIronCladDoor, SOUND_DESTROYDOOR);
                }
            }

			void DoPlaySound(GameObject* unit, uint32 sound)
            {
                unit->PlayDirectSound(sound);
            }

			void SetData(uint32 type, uint32 data)
			{
				switch (type)
				{
				    case DATA_CANNON_EVENT:
					    State = data;
					    if (data == CANNON_BLAST_INITIATED)
					    {
						    ShootCannon();
						    BlastOutDoor();
					    }	
					    break;
                }
            }

            ObjectGuid GetGuidData(uint32 data) const
            {
                switch (data)
                {
                    case DATA_GLUBTOK:
                        return uiGlubtokGUID;
                    case DATA_HELIX:
                        return uiHelixGUID;
                    case DATA_OAF:
                        return uiOafGUID;
                    case DATA_FOEREAPER:
                        return uiFoereaperGUID;
					case DATA_ADMIRAL:
						return uiAdmiralGUID;
                }
                return ObjectGuid::Empty;
            }

            std::string GetSaveData()
            {
                OUT_SAVE_INST_DATA;

                std::string str_data;
                std::ostringstream saveStream;
                saveStream << "D M " << GetBossSaveData() << State;
                str_data = saveStream.str();

                OUT_SAVE_INST_DATA_COMPLETE;
                return str_data;
            }

            void Load(const char* in)
            {
                if (!in)
                {
                    OUT_LOAD_INST_DATA_FAIL;
                    return;
                }

                OUT_LOAD_INST_DATA(in);

                char dataHead1, dataHead2;

                std::istringstream loadStream(in);
                loadStream >> dataHead1 >> dataHead2;

                if (dataHead1 == 'D' && dataHead2 == 'M')
                {

                    for (uint8 i = 0; i < MAX_ENCOUNTER; ++i)
				    {
					    uint32 tmpState;
					    loadStream >> tmpState;
					    if (tmpState == IN_PROGRESS || tmpState > SPECIAL)
						    tmpState = NOT_STARTED;
					    SetBossState(i, EncounterState(tmpState));
				    }

                    loadStream >> State;

				    if (State == CANNON_BLAST_INITIATED)
					    if (GameObject *pIronCladDoor = instance->GetGameObject(IronCladDoorGUID))
						    pIronCladDoor->SetGoState(GO_STATE_ACTIVE_ALTERNATIVE);

                }
                else OUT_LOAD_INST_DATA_FAIL;

                OUT_LOAD_INST_DATA_COMPLETE;
            }

        private:
            ObjectGuid uiGlubtokGUID;
            ObjectGuid uiHelixGUID;
            ObjectGuid uiOafGUID;
            ObjectGuid uiFoereaperGUID;
			ObjectGuid uiAdmiralGUID;
			ObjectGuid uiCaptainGUID;

            ObjectGuid FactoryDoorGUID;
            ObjectGuid FoundryDoorGUID;
            ObjectGuid MastRoomDoorGUID;
            ObjectGuid IronCladDoorGUID;
            ObjectGuid DefiasCannonGUID;
            ObjectGuid DoorLeverGUID;
            ObjectGuid DefiasPirate1GUID;
            ObjectGuid DefiasPirate2GUID;
            ObjectGuid DefiasCompanionGUID;

            uint32 State;

        };
};

void AddSC_instance_deadmines()
{
    new instance_deadmines();
}
