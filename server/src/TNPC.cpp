#include "IDebug.h"
#include <vector>
#include <time.h>
#include "TServer.h"
#include "TNPC.h"
#include "CFileSystem.h"
#include "TMap.h"
#include "TLevel.h"
#include "IEnums.h"

#ifdef V8NPCSERVER
#include "CScriptEngine.h"
#include "TPlayer.h"
#endif

const char __nSavePackets[10] = { 23, 24, 25, 26, 27, 28, 29, 30, 31, 32 };
const char __nAttrPackets[30] = { 36, 37, 38, 39, 40, 44, 45, 46, 47, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73 };

static CString toWeaponName(const CString& code);
static CString doJoins(const CString& code, CFileSystem* fs);

TNPC::TNPC(const CString& pImage, const CString& pScript, float pX, float pY, TServer* pServer, TLevel* pLevel, bool pLevelNPC, bool trimCode)
:
blockPositionUpdates(false),
levelNPC(pLevelNPC),
x(pX), y(pY), hurtX(32.0f), hurtY(32.0f),
x2((int)(pX*16)), y2((int)(pY*16)),
gmaplevelx(0), gmaplevely(0),
id(0), rupees(0),
darts(0), bombs(0), glovePower(0), bombPower(0), swordPower(0), shieldPower(0),
visFlags(1), blockFlags(0), sprite(2), power(0), ap(50),
image(pImage), gani("idle"),
level(pLevel), server(pServer)
#ifdef V8NPCSERVER
, width(32), height(32), timeout(0), _scriptEventsMask(0), _scriptObject(0)
#endif
{
	memset((void*)colors, 0, sizeof(colors));
	memset((void*)saves, 0, sizeof(saves));
	memset((void*)modTime, 0, sizeof(modTime));

	// bowImage for pre-2.x clients.
	bowImage >> (char)0;

	// imagePart needs to be Graal-packed.
	for (int i = 0; i < 6; i++)
		imagePart.writeGChar(0);

	// Set the gmap levels.
	TMap* gmap = level->getMap();
	if (gmap && gmap->getType() == MAPTYPE_GMAP)
	{
		gmaplevelx = (unsigned char)gmap->getLevelX(level->getLevelName());
		gmaplevely = (unsigned char)gmap->getLevelY(level->getLevelName());
	}

	// We need to alter the modTime of the following props as they should be always sent.
	// If we don't, they won't be sent until the prop gets modified.
	modTime[NPCPROP_IMAGE] = modTime[NPCPROP_SCRIPT] = modTime[NPCPROP_X] = modTime[NPCPROP_Y]
		= modTime[NPCPROP_VISFLAGS] = modTime[NPCPROP_ID] = modTime[NPCPROP_SPRITE] = modTime[NPCPROP_MESSAGE]
		= modTime[NPCPROP_GMAPLEVELX] = modTime[NPCPROP_GMAPLEVELY]
		= modTime[NPCPROP_X2] = modTime[NPCPROP_Y2] = time(0);

	bool levelModificationNPCHack = false;

	// See if the NPC sets the level as a sparring zone.
	if (pScript.subString(0, 12) == "sparringzone")
	{
		pLevel->setSparringZone(true);
		levelModificationNPCHack = true;
	}

	// See if the NPC sets the level as singleplayer.
	if (pScript.subString(0, 12) == "singleplayer")
	{
		pLevel->setSingleplayer(true);
		levelModificationNPCHack = true;
	}

	// Separate clientside and serverside scripts.
#ifdef V8NPCSERVER
	CString s = pScript;
	serverScript = s.readString("//#CLIENTSIDE");
	clientScript = s.readString("");
#else
	if (server->hasNPCServer())
	{
		if (levelModificationNPCHack)
			serverScript = clientScript = pScript;
		else
		{
			CString s = pScript;
			serverScript = s.readString("//#CLIENTSIDE");
			clientScript = s.readString("");
		}
	}
	else clientScript = pScript;
#endif

	// Do joins.
	if (!serverScript.isEmpty()) serverScript = doJoins(serverScript, server->getFileSystem());
	if (!clientScript.isEmpty()) clientScript = doJoins(clientScript, server->getFileSystem());

	// See if the NPC should block position updates from the level leader.
	if (server->hasNPCServer())
	{
		if (serverScript.find("//#BLOCKPOSITIONUPDATES") != -1)
			blockPositionUpdates = true;
	}
	else
	{
		if (clientScript.find("//#BLOCKPOSITIONUPDATES") != -1)
			blockPositionUpdates = true;
	}

	// Remove comments and trim the code if specified.
	if (trimCode)
	{
		if (!serverScript.isEmpty())
		{
			serverScriptFormatted = removeComments(serverScript, "\xa7");
			std::vector<CString> code = serverScriptFormatted.tokenize("\xa7");
			serverScriptFormatted.clear();
			for (std::vector<CString>::iterator i = code.begin(); i != code.end(); ++i)
				serverScriptFormatted << (*i).trim() << "\xa7";
		}
		if (!clientScript.isEmpty())
		{
			clientScriptFormatted = removeComments(clientScript, "\xa7");
			std::vector<CString> code = clientScriptFormatted.tokenize("\xa7");
			clientScriptFormatted.clear();
			for (std::vector<CString>::iterator i = code.begin(); i != code.end(); ++i)
				clientScriptFormatted << (*i).trim() << "\xa7";
		}
	}

	// Search for toweapons in the clientside code and extract the name of the weapon.
	weaponName = toWeaponName(clientScript);

	// Just a little warning for people who don't know.
	if (clientScriptFormatted.length() > 0x705F)
		printf("WARNING: Clientside script of NPC (%s) exceeds the limit of 28767 bytes.\n", (weaponName.length() != 0 ? weaponName.text() : image.text()));

	// TODO: Create plugin hook so NPCServer can acquire/format code.
}

TNPC::~TNPC()
{
#ifdef V8NPCSERVER
	server->getScriptEngine()->UnregisterNpcUpdate(this);
	for (auto it = _actions.begin(); it != _actions.end(); ++it)
		delete *it;

	if (_scriptObject)
		delete _scriptObject;
#endif
}

CString TNPC::getProp(unsigned char pId, int clientVersion) const
{
	switch(pId)
	{
		case NPCPROP_IMAGE:
		return CString() >> (char)image.length() << image;

		case NPCPROP_SCRIPT:
		{
			if (clientVersion != NSVER_GENERIC)
				return CString() >> (short)(clientScriptFormatted.length() > 0x3FFF ? 0x3FFF : clientScriptFormatted.length()) << clientScriptFormatted.subString(0, 0x3FFF);
			else
				return CString() >> (long long)serverScriptFormatted.length() << serverScriptFormatted;
		}

		case NPCPROP_X:
		return CString() >> (char)(x * 2);

		case NPCPROP_Y:
		return CString() >> (char)(y * 2);

		case NPCPROP_POWER:
		return CString() >> (char)power;

		case NPCPROP_RUPEES:
		return CString() >> (int)rupees;

		case NPCPROP_ARROWS:
		return CString() >> (char)darts;

		case NPCPROP_BOMBS:
		return CString() >> (char)bombs;

		case NPCPROP_GLOVEPOWER:
		return CString() >> (char)glovePower;

		case NPCPROP_BOMBPOWER:
		return CString() >> (char)bombPower;

		case NPCPROP_SWORDIMAGE:
		if (swordPower == 0)
			return CString() >> (char)0;
		else
			return CString() >> (char)(swordPower + 30) >> (char)swordImage.length() << swordImage;

		case NPCPROP_SHIELDIMAGE:
		if (shieldPower + 10 > 10)
			return CString() >> (char)(shieldPower + 10) >> (char)shieldImage.length() << shieldImage;
		else
			return CString() >> (char)0;

		case NPCPROP_GANI:
		if (clientVersion < CLVER_2_1)
			return bowImage;
		return CString() >> (char)gani.length() << gani;

		case NPCPROP_VISFLAGS:
		return CString() >> (char)visFlags;

		case NPCPROP_BLOCKFLAGS:
		return CString() >> (char)blockFlags;

		case NPCPROP_MESSAGE:
		return CString() >> (char)chatMsg.subString(0, 200).length() << chatMsg.subString(0, 200);

		case NPCPROP_HURTDXDY:
		return CString() >> (char)((hurtX*32)+32) >> (char)((hurtY*32)+32);

		case NPCPROP_ID:
		return CString() >> (int)id;

		// Sprite is deprecated and has been replaced by def.gani.
		// Sprite now holds the direction of the npc.  sprite % 4 gives backwards compatibility.
		case NPCPROP_SPRITE:
		{
			if (clientVersion < CLVER_2_1) return CString() >> (char)sprite;
			else return CString() >> (char)(sprite % 4);
		}

		case NPCPROP_COLORS:
		return CString() >> (char)colors[0] >> (char)colors[1] >> (char)colors[2] >> (char)colors[3] >> (char)colors[4];

		case NPCPROP_NICKNAME:
		return CString() >> (char)nickName.length() << nickName;

		case NPCPROP_HORSEIMAGE:
		return CString() >> (char)horseImage.length() << horseImage;

		case NPCPROP_HEADIMAGE:
		return CString() >> (char)(headImage.length() + 100) << headImage;

		case NPCPROP_ALIGNMENT:
		return CString() >> (char)ap;

		case NPCPROP_IMAGEPART:
		return CString() << imagePart;

		case NPCPROP_BODYIMAGE:
		return CString() >> (char)bodyImage.length() << bodyImage;

		case NPCPROP_GMAPLEVELX:
		return CString() >> (char)(level && level->getMap() ? level->getMap()->getLevelX(level->getActualLevelName()) : 0);

		case NPCPROP_GMAPLEVELY:
		return CString() >> (char)(level && level->getMap() ? level->getMap()->getLevelY(level->getActualLevelName()) : 0);

		case NPCPROP_CLASS:
		return CString() >> (short)0;

		case NPCPROP_X2:
		{
			unsigned short val = (x2 < 0 ? -x2 : x2);
			val <<= 1;
			if (x2 < 0) val |= 0x0001;
			return CString().writeGShort(val);
		}

		case NPCPROP_Y2:
		{
			unsigned short val = (y2 < 0 ? -y2 : y2);
			val <<= 1;
			if (y2 < 0) val |= 0x0001;
			return CString().writeGShort((short)val);
		}
	}

	// Saves.
	if (inrange(pId, 23, 32))
	{
		for (unsigned int i = 0; i < sizeof(__nSavePackets); i++)
		{
			if (__nSavePackets[i] == pId)
				return CString() >> (char)saves[i];
		}
	}

	// Gani attributes.
	if (inrange(pId, NPCPROP_GATTRIB1, NPCPROP_GATTRIB5) || inrange(pId, NPCPROP_GATTRIB6, NPCPROP_GATTRIB9) || inrange(pId, NPCPROP_GATTRIB10, NPCPROP_GATTRIB30))
	{
		for (unsigned int i = 0; i < sizeof(__nAttrPackets); i++)
		{
			if (__nAttrPackets[i] == pId)
				return CString() >> (char)gAttribs[i].length() << gAttribs[i];
		}
	}

	return CString();
}

CString TNPC::getProps(time_t newTime, int clientVersion) const
{
	bool oldcreated = server->getSettings()->getBool("oldcreated", "false");
	CString retVal;
	int pmax = NPCPROP_COUNT;
	if (clientVersion < CLVER_2_1) pmax = 36;

	for (int i = 0; i < pmax; i++)
	{
		if (modTime[i] != 0 && modTime[i] >= newTime)
		{
			if (oldcreated && i == NPCPROP_VISFLAGS && newTime == 0)
				retVal >> (char)i >> (char)(visFlags | NPCVISFLAG_VISIBLE);
			else
				retVal >> (char)i << getProp(i, clientVersion);
		}
	}
	if (clientVersion > CLVER_1_411)
	{
		if (modTime[NPCPROP_GANI] == 0 && image == "#c#")
			retVal >> (char)NPCPROP_GANI >> (char)4 << "idle";
	}

	return retVal;
}

CString TNPC::setProps(CString& pProps, int clientVersion, bool pForward)
{
	CString ret;
	int len = 0;
	while (pProps.bytesLeft() > 0)
	{
		unsigned char propId = pProps.readGUChar();
		CString oldProp = getProp(propId);
		//printf( "propId: %d\n", propId );
		switch (propId)
		{
			case NPCPROP_IMAGE:
				image = pProps.readChars(pProps.readGUChar());
				if (!image.isEmpty() && clientVersion < CLVER_2_1 && getExtension(image).isEmpty())
					image << ".gif";
			break;

			case NPCPROP_SCRIPT:
				clientScript = pProps.readChars(pProps.readGUShort());
			break;

			case NPCPROP_X:
				if (blockPositionUpdates)
				{
					pProps.readGChar();
					continue;
				}
				x = (float)(pProps.readGChar()) / 2.0f;
				x2 = (int)(x * 16);
			break;

			case NPCPROP_Y:
				if (blockPositionUpdates)
				{
					pProps.readGChar();
					continue;
				}
				y = (float)(pProps.readGChar()) / 2.0f;
				y2 = (int)(y * 16);
			break;

			case NPCPROP_POWER:
				power = pProps.readGUChar();
			break;

			case NPCPROP_RUPEES:
				rupees = pProps.readGUInt();
			break;

			case NPCPROP_ARROWS:
				darts = pProps.readGUChar();
			break;

			case NPCPROP_BOMBS:
				bombs = pProps.readGUChar();
			break;

			case NPCPROP_GLOVEPOWER:
				glovePower = pProps.readGUChar();
			break;

			case NPCPROP_BOMBPOWER:
				bombPower = pProps.readGUChar();
			break;

			case NPCPROP_SWORDIMAGE:
			{
				int sp = pProps.readGUChar();
				if (sp <= 4)
					swordImage = CString() << "sword" << CString(sp) << (clientVersion < CLVER_2_1 ? ".gif" : ".png");
				else
				{
					sp -= 30;
					len = pProps.readGUChar();
					if (len > 0)
					{
						swordImage = pProps.readChars(len);
						if (!swordImage.isEmpty() && clientVersion < CLVER_2_1 && getExtension(swordImage).isEmpty())
							swordImage << ".gif";
					}
					else swordImage = "";
					//swordPower = clip(sp, ((settings->getBool("healswords", false) == true) ? -(settings->getInt("swordlimit", 3)) : 0), settings->getInt("swordlimit", 3));
				}
				swordPower = sp;
			}
			break;

			case NPCPROP_SHIELDIMAGE:
			{
				int sp = pProps.readGUChar();
				if (sp <= 3)
					shieldImage = CString() << "shield" << CString(sp) << (clientVersion < CLVER_2_1 ? ".gif" : ".png");
				else
				{
					sp -= 10;
					len = pProps.readGUChar();
					if (len > 0)
					{
						shieldImage = pProps.readChars(len);
						if (!shieldImage.isEmpty() && clientVersion < CLVER_2_1 && getExtension(shieldImage).isEmpty())
							shieldImage << ".gif";
					}
					else shieldImage = "";
				}
				shieldPower = sp;
			}
			break;

			case NPCPROP_GANI:
				if (clientVersion < CLVER_2_1)
				{
					// Older clients don't use ganis.  This is the bow power and image instead.
					int sp = pProps.readGUChar();
					if (sp < 10)
						bowImage = CString() >> (char)sp;
					else
					{
						sp -= 10;
						if (sp < 0) break;
						bowImage = pProps.readChars(sp);
						if (!bowImage.isEmpty() && clientVersion < CLVER_2_1 && getExtension(bowImage).isEmpty())
							bowImage << ".gif";
						bowImage = CString() >> (char)(10 + bowImage.length()) << bowImage;
					}
					break;
				}
				gani = pProps.readChars(pProps.readGUChar());
			break;

			case NPCPROP_VISFLAGS:
				visFlags = pProps.readGUChar();
			break;

			case NPCPROP_BLOCKFLAGS:
				blockFlags = pProps.readGUChar();
			break;

			case NPCPROP_MESSAGE:
				chatMsg = pProps.readChars(pProps.readGUChar());
			break;

			case NPCPROP_HURTDXDY:
				hurtX = ((float)(pProps.readGUChar()-32))/32;
				hurtY = ((float)(pProps.readGUChar()-32))/32;
			break;

			case NPCPROP_ID:
				pProps.readGUInt();
			break;

			case NPCPROP_SPRITE:
				sprite = pProps.readGUChar();
			break;

			case NPCPROP_COLORS:
				for (int i = 0; i < 5; i++)
					colors[i] = pProps.readGUChar();
			break;

			case NPCPROP_NICKNAME:
				nickName = pProps.readChars(pProps.readGUChar());
			break;

			case NPCPROP_HORSEIMAGE:
				horseImage = pProps.readChars(pProps.readGUChar());
				if (!horseImage.isEmpty() && clientVersion < CLVER_2_1 && getExtension(horseImage).isEmpty())
					horseImage << ".gif";
			break;

			case NPCPROP_HEADIMAGE:
				len = pProps.readGUChar();
				if (len < 100)
					headImage = CString() << "head" << CString(len) << (clientVersion < CLVER_2_1 ? ".gif" : ".png");
				else
				{
					headImage = pProps.readChars(len - 100);
					if (!headImage.isEmpty() && clientVersion < CLVER_2_1 && getExtension(headImage).isEmpty())
						headImage << ".gif";
				}
			break;

			case NPCPROP_ALIGNMENT:
				ap = pProps.readGUChar();
				ap = clip(ap, 0, 100);
			break;

			case NPCPROP_IMAGEPART:
				imagePart = pProps.readChars(6);
			break;

			case NPCPROP_BODYIMAGE:
				bodyImage = pProps.readChars(pProps.readGUChar());
			break;

			case NPCPROP_GMAPLEVELX:
				gmaplevelx = pProps.readGUChar();
			break;

			case NPCPROP_GMAPLEVELY:
				gmaplevely = pProps.readGUChar();
			break;

			case NPCPROP_CLASS:
				pProps.readChars(pProps.readGShort());
			break;

			// Location, in pixels, of the npc on the level in 2.3+ clients.
			// Bit 0x0001 controls if it is negative or not.
			// Bits 0xFFFE are the actual value.
			case NPCPROP_X2:
				if (blockPositionUpdates)
				{
					pProps.readGUShort();
					continue;
				}

				x2 = len = pProps.readGUShort();

				// If the first bit is 1, our position is negative.
				x2 >>= 1;
				if ((short)len & 0x0001) x2 = -x2;
				
				// Let pre-2.3+ clients see 2.3+ movement.
				x = (float)x2 / 16.0f;
				break;

			case NPCPROP_Y2:
				if (blockPositionUpdates)
				{
					pProps.readGUShort();
					continue;
				}

				y2 = len = pProps.readGUShort();

				// If the first bit is 1, our position is negative.
				y2 >>= 1;
				if ((short)len & 0x0001) y2 = -y2;

				// Let pre-2.3+ clients see 2.3+ movement.
				y = (float)y2 / 16.0f;
				break;

			case NPCPROP_SAVE0: saves[0] = pProps.readGUChar(); break;
			case NPCPROP_SAVE1: saves[1] = pProps.readGUChar(); break;
			case NPCPROP_SAVE2: saves[2] = pProps.readGUChar(); break;
			case NPCPROP_SAVE3: saves[3] = pProps.readGUChar(); break;
			case NPCPROP_SAVE4: saves[4] = pProps.readGUChar(); break;
			case NPCPROP_SAVE5: saves[5] = pProps.readGUChar(); break;
			case NPCPROP_SAVE6: saves[6] = pProps.readGUChar(); break;
			case NPCPROP_SAVE7: saves[7] = pProps.readGUChar(); break;
			case NPCPROP_SAVE8: saves[8] = pProps.readGUChar(); break;
			case NPCPROP_SAVE9: saves[9] = pProps.readGUChar(); break;

			case NPCPROP_GATTRIB1:  gAttribs[0]  = pProps.readChars(pProps.readGUChar()); break;
			case NPCPROP_GATTRIB2:  gAttribs[1]  = pProps.readChars(pProps.readGUChar()); break;
			case NPCPROP_GATTRIB3:  gAttribs[2]  = pProps.readChars(pProps.readGUChar()); break;
			case NPCPROP_GATTRIB4:  gAttribs[3]  = pProps.readChars(pProps.readGUChar()); break;
			case NPCPROP_GATTRIB5:  gAttribs[4]  = pProps.readChars(pProps.readGUChar()); break;
			case NPCPROP_GATTRIB6:  gAttribs[5]  = pProps.readChars(pProps.readGUChar()); break;
			case NPCPROP_GATTRIB7:  gAttribs[6]  = pProps.readChars(pProps.readGUChar()); break;
			case NPCPROP_GATTRIB8:  gAttribs[7]  = pProps.readChars(pProps.readGUChar()); break;
			case NPCPROP_GATTRIB9:  gAttribs[8]  = pProps.readChars(pProps.readGUChar()); break;
			case NPCPROP_GATTRIB10: gAttribs[9]  = pProps.readChars(pProps.readGUChar()); break;
			case NPCPROP_GATTRIB11: gAttribs[10] = pProps.readChars(pProps.readGUChar()); break;
			case NPCPROP_GATTRIB12: gAttribs[11] = pProps.readChars(pProps.readGUChar()); break;
			case NPCPROP_GATTRIB13: gAttribs[12] = pProps.readChars(pProps.readGUChar()); break;
			case NPCPROP_GATTRIB14: gAttribs[13] = pProps.readChars(pProps.readGUChar()); break;
			case NPCPROP_GATTRIB15: gAttribs[14] = pProps.readChars(pProps.readGUChar()); break;
			case NPCPROP_GATTRIB16: gAttribs[15] = pProps.readChars(pProps.readGUChar()); break;
			case NPCPROP_GATTRIB17: gAttribs[16] = pProps.readChars(pProps.readGUChar()); break;
			case NPCPROP_GATTRIB18: gAttribs[17] = pProps.readChars(pProps.readGUChar()); break;
			case NPCPROP_GATTRIB19: gAttribs[18] = pProps.readChars(pProps.readGUChar()); break;
			case NPCPROP_GATTRIB20: gAttribs[19] = pProps.readChars(pProps.readGUChar()); break;
			case NPCPROP_GATTRIB21: gAttribs[20] = pProps.readChars(pProps.readGUChar()); break;
			case NPCPROP_GATTRIB22: gAttribs[21] = pProps.readChars(pProps.readGUChar()); break;
			case NPCPROP_GATTRIB23: gAttribs[22] = pProps.readChars(pProps.readGUChar()); break;
			case NPCPROP_GATTRIB24: gAttribs[23] = pProps.readChars(pProps.readGUChar()); break;
			case NPCPROP_GATTRIB25: gAttribs[24] = pProps.readChars(pProps.readGUChar()); break;
			case NPCPROP_GATTRIB26: gAttribs[25] = pProps.readChars(pProps.readGUChar()); break;
			case NPCPROP_GATTRIB27: gAttribs[26] = pProps.readChars(pProps.readGUChar()); break;
			case NPCPROP_GATTRIB28: gAttribs[27] = pProps.readChars(pProps.readGUChar()); break;
			case NPCPROP_GATTRIB29: gAttribs[28] = pProps.readChars(pProps.readGUChar()); break;
			case NPCPROP_GATTRIB30: gAttribs[29] = pProps.readChars(pProps.readGUChar()); break;

			default:
			{
				printf("NPC %d (%.2f, %.2f): ", id, x, y);
				printf("Unknown prop: %ud, readPos: %d\n", propId, pProps.readPos());
				for (int i = 0; i < pProps.length(); ++i)
					printf("%02x ", (unsigned char)pProps[i]);
				printf("\n");
			}
			return ret;
		}

		// If a prop changed, adjust its mod time.
		if (propId < NPCPROP_COUNT)
		{
			if (oldProp != getProp(propId))
				modTime[propId] = time(0);
		}

		// Add to ret.
		ret >> (char)propId << getProp(propId, clientVersion);
	}

	if (pForward) {
		// Find the level.
		TMap* map = 0;
		if (level != 0) map = level->getMap();

		// Send the props.
		// TODO(joey): only gmap? why is this?
		server->sendPacketToLevel(CString() >> (char)PLO_NPCPROPS >> (int)id << ret, map, level, 0, true);
	}

	return ret;
}

#ifdef V8NPCSERVER

void TNPC::setTimeout(int newTimeout) {
	if (newTimeout > 0)
		server->getScriptEngine()->RegisterNpcTimer(this);
	else if (timeout <= 0)
		server->getScriptEngine()->UnregisterNpcTimer(this);
	timeout = newTimeout;
}

void TNPC::queueNpcAction(const std::string& action, TPlayer *player, bool registerAction)
{
	// TODO(joey): profile strings in release, it is slowing down execution in debug.
	
	// TODO(joey): ScriptArguments does not do sanity checks on passed objects
	// still debating on whether I should check prior to running the action or
	// implement sanity check in the Server.
	
	ScriptAction *scriptAction = 0;
	CScriptEngine *scriptEngine = server->getScriptEngine();

	IScriptWrapped<TPlayer> *playerObject = 0;
	if (player != 0)
	{
		auto playerObject = player->getScriptObject();
		if (playerObject != 0)
			scriptAction = scriptEngine->CreateAction(action, _scriptObject, playerObject);
	}
	
	if (!scriptAction)
		scriptAction = scriptEngine->CreateAction(action, _scriptObject);

	//IScriptWrapped<TPlayer> *playerObject = 0;
	//if (player != 0)
	//	playerObject = player->getScriptObject();

	//CScriptEngine *scriptEngine = server->getScriptEngine();
	//ScriptAction *scriptAction = scriptEngine->CreateAction(action, playerObject);
	
	_actions.push_back(scriptAction);
	if (registerAction)
		scriptEngine->RegisterNpcUpdate(this);
}

bool TNPC::runScriptTimer()
{
	if (timeout > 0)
	{
		timeout--;
		if (timeout == 0)
			queueNpcAction("npc.timeout", 0, true);
	}
	
	// return value dictates if this gets deregistered from updates
	return (timeout > 0);
}

void TNPC::runScriptEvents()
{
	// iterate over queued actions
	for (auto it = _actions.begin(); it != _actions.end(); ++it)
	{
		ScriptAction *action = *it;
		if (action != 0)
		{
			V8ENV_D("Running action: %s\n", action->getAction().c_str());
			action->Invoke();
			delete action;
		}
	}
	_actions.clear();
}

#endif

CString toWeaponName(const CString& code)
{
	int name_start = code.find("toweapons ");
	if (name_start == -1) return CString();
	name_start += 10;	// 10 = strlen("toweapons ")

	int name_end[2] = { code.find(";", name_start), code.find("}", name_start) };
	if (name_end[0] == -1 && name_end[1] == -1) return CString();

	int name_pos = -1;
	if (name_end[0] == -1) name_pos = name_end[1];
	if (name_end[1] == -1) name_pos = name_end[0];
	if (name_pos == -1) name_pos = (name_end[0] < name_end[1]) ? name_end[0] : name_end[1];

	return code.subString(name_start, name_pos - name_start).trim();
}

CString doJoins(const CString& code, CFileSystem* fs)
{
	CString ret;
	CString c(code);
	std::vector<CString> joinList;

	// Parse out all the joins.
	while (c.bytesLeft())
	{
		ret << c.readString("join ");

		int pos = c.readPos();
		int loc = c.find(";", pos);
		if (loc != -1)
		{
			CString spacecheck = c.subString(pos, loc - pos);
			if (!spacecheck.contains(" \t") && c.bytesLeft())
			{
				ret << ";\xa7";
				joinList.push_back(CString() << c.readString(";") << ".txt");
			}
		}
	}

	// Add the files now.
	for (std::vector<CString>::iterator i = joinList.begin(); i != joinList.end(); ++i)
	{
		//printf("file: %s\n", (*i).text());
		c = fs->load(*i);
		c.removeAllI("\r");
		c.replaceAllI("\n", "\xa7");
		ret << c;
		//ret << removeComments(c, "\xa7");
	}

	return ret;
}
