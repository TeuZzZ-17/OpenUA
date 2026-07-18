#include <inttypes.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#include <vector>
#include <functional>
#include <set>
#include "env.h"
#include "includes.h"
#include "yw_internal.h"
#include "yw.h"
#include "yw_net.h"
#include "windp.h"
#include "loaders.h"

#include "yparobo.h"
#include "font.h"
#include "gui/uacommon.h"
#include "system/inivals.h"
#include "system/system.h"
#include "world/spin.h"

extern uint32_t bact_id;


NC_STACK_bitmap * loadDisk_screen(NC_STACK_ypaworld *yw)
{
    SFXEngine::SFXe.StopMusicTrack();

    const char *v3;

    if ( yw->_screenSize.x <= 360 )
        v3 = "disk320.ilbm";
    else if ( yw->_screenSize.x > 600 )
        v3 = "disk640.ilbm";
    else
        v3 = "disk512.ilbm";

    std::string oldRsrc = Common::Env.SetPrefix("rsrc", "data:mc2res");

    NC_STACK_bitmap *disk = Utils::ProxyLoadImage({
        {NC_STACK_rsrc::RSRC_ATT_NAME, std::string(v3)},
        {NC_STACK_bitmap::BMD_ATT_CONVCOLOR, (int32_t)1}});

    Common::Env.SetPrefix("rsrc", oldRsrc);

    return disk;
}

void draw_splashScreen(NC_STACK_ypaworld *yw, NC_STACK_bitmap *splashScreen)
{
    if ( splashScreen )
    {
        GFX::rstr_arg204 a4;

        a4.pbitm = splashScreen->GetBitmap();

        a4.float4  = Common::FRect(-1.0, -1.0, 1.0, 1.0);
        a4.float14 = Common::FRect(-1.0, -1.0, 1.0, 1.0);

        GFX::displ_arg263 v4;
        if (yw->_mousePointers[5])
            v4.bitm = yw->_mousePointers[5]->GetBitmap();
        v4.pointer_id = 6;

        GFX::Engine.SetCursor(v4.pointer_id, 0);

        GFX::Engine.BeginFrame();
        GFX::Engine.raster_func202(&a4);
        GFX::Engine.EndFrame();

        GFX::Engine.BeginFrame();
        GFX::Engine.raster_func202(&a4);
        GFX::Engine.EndFrame();
    }
}

void drawSplashScreenWithTOD(NC_STACK_ypaworld *yw, NC_STACK_bitmap *splashScreen, const std::string &text)
{
    if ( splashScreen )
    {
        GFX::rstr_arg204 a4;

        a4.pbitm = splashScreen->GetBitmap();

        a4.float4  = Common::FRect(-1.0, -1.0, 1.0, 1.0);
        a4.float14 = Common::FRect(-1.0, -1.0, 1.0, 1.0);

        GFX::displ_arg263 v4;
        if (yw->_mousePointers[5])
            v4.bitm = yw->_mousePointers[5]->GetBitmap();
        v4.pointer_id = 6;

        GFX::Engine.SetCursor(v4.pointer_id, 0);

        GFX::Engine.BeginFrame();
        GFX::Engine.raster_func202(&a4);
        splashScreen_OutText(yw, text, yw->_screenSize.x / 7, yw->_screenSize.y / 5);
        GFX::Engine.EndFrame();

        GFX::Engine.BeginFrame();
        GFX::Engine.raster_func202(&a4);
        splashScreen_OutText(yw, text, yw->_screenSize.x / 7, yw->_screenSize.y / 5);
        GFX::Engine.EndFrame();
    }
}

void NC_STACK_ypaworld::PowerStationErase(cellArea *cell)
{
    if (!cell) return;

    auto it = _powerStations.find(cell->Id);
    if (it == _powerStations.end())
    {
        /* Oh no, power station info for this sector does not exist.
           So just clear sector purpose field if it was PowerStation*/
        if (cell->PurposeType == cellArea::PT_POWERSTATION)
        {
            cell->PurposeIndex = 0;
            cell->PurposeType = cellArea::PT_NONE;
        }

        return;
    }

    _powerStations.erase(it);

    cell->PurposeIndex = 0;
    cell->PurposeType = cellArea::PT_NONE;

    _lvlBuildingsMap(cell->CellId) = 0;
}

void sb_0x44ca90__sub2(NC_STACK_ypaworld *yw, TLevelDescription *mapp)
{
    if (!mapp->Palettes.empty())
    {
        if (!mapp->Palettes[0].empty())
        {
            // OpenUA: legacy palette-theme remap is bypassed (GetPaletteThemeOverridePath is a
            // passthrough), so the level base palette (slot0) always loads as in vanilla.
            std::string palettePath = GFX::Engine.GetPaletteThemeOverridePath(mapp->Palettes[0]);
            std::string oldRsrc;
            if (palettePath != mapp->Palettes[0])
                oldRsrc = Common::Env.SetPrefix("rsrc", "data:");

            NC_STACK_bitmap *ilbm = Utils::ProxyLoadImage({
                {NC_STACK_rsrc::RSRC_ATT_NAME, palettePath},
                {NC_STACK_bitmap::BMD_ATT_HAS_COLORMAP, (int32_t)1}});

            if (palettePath != mapp->Palettes[0])
                Common::Env.SetPrefix("rsrc", oldRsrc);

            if (!ilbm && palettePath != mapp->Palettes[0])
            {
                ypa_log_out("WARNING: Could not load palette theme [%s] from [%s], falling back to [%s].\n",
                            System::IniConf::GfxPaletteTheme.Get<std::string>().c_str(),
                            palettePath.c_str(),
                            mapp->Palettes[0].c_str());

                ilbm = Utils::ProxyLoadImage({
                    {NC_STACK_rsrc::RSRC_ATT_NAME, mapp->Palettes[0]},
                    {NC_STACK_bitmap::BMD_ATT_HAS_COLORMAP, (int32_t)1}});
            }

            if (ilbm)
            {
                GFX::Engine.SetPalette(*ilbm->getBMD_palette());
                ilbm->Delete();
            }
            else
            {
                ypa_log_out("WARNING: slot #%d [%s] init failed!\n", 0, mapp->Palettes[0].c_str());
            }
        }
    }
}

int NC_STACK_ypaworld::LevelCommonLoader(TLevelDescription *mapp, int levelID, int a5)
{
    int ok = 0;

    *mapp = TLevelDescription();

    _gameplayStats.fill( World::TPlayerStatus() );

    _debugAoeRings.clear();
    _timeStamp = 0;
    _msgTimestampHSReturn = 0;
    _msgTimestampEnemySector = 0;
    _msgTimestampGates = 0;
    _msgTimestampPSUnderAtk = 0;
    _framesElapsed = 0;

    _levelInfo.LevelID = levelID;
    _levelInfo.Mode = a5;
    _levelInfo.State = TLevelInfo::STATE_PLAYING;
    _levelInfo.OwnerMask = 0;
    _levelInfo.UserMask = 0;

    _cellOnMouse = 0;
    _bactOnMouse = NULL;
    _damageHoverTargets.clear();
    _bactPrevClicked = 0;
    ClearMortarMarkers();
    _mortarManualGid = 0;
    _mortarManualRadius = 0.0f;
    _viewerBact = NULL;
    _userRobo = NULL;
    _userUnit = NULL;
    _makingWaypointsMode = false;
    _gamePaused = false;
    _gamePausedTimeStamp = 0;
    _debugGameplayFrozen = false;
    _joyIgnoreX = 1;
    _joyIgnoreY = 1;
    _joyIgnoreZ = 0;
    _helpURL.clear();
    _prevUnitId = 0;
    _fireBtnIsDown = false;
    _fireBtnDownHappen = false;
    _playerHSDestroyed = false;
    _vehicleSectorRatio = 0;
    _beamEnergyCurrent = 0;
    _invulnerable = 0;

    _levelInfo.Gates.clear();
    _levelInfo.SuperItems.clear();

    _techUpgrades.clear();

    _netEvent = TNetGameEvent();
    _countUnitsPerOwner.fill(0);

    _dbgTotalSquadCountMax = 0;
    _dbgTotalVehicleCountMax = 0;
    _dbgTotalFlakCountMax = 0;
    _dbgTotalWeaponCountMax = 0;
    _dbgTotalRoboCountMax = 0;

    _playerOwner = 0;

    /* Set hidden fractions to default world's*/
    _hiddenFractions = _worldHiddenFractions;

    if ( _gfxMode != GFX::Engine.GetGfxMode() || _GameShell->IsWindowedFlag() != GFX::Engine.GetGfxMode().windowed )
    {
        GFX::Engine.SetResolution(_gfxMode, _GameShell->IsWindowedFlag());

        _screenSize = GFX::Engine.GetScreenSize();

        GFX::Engine.setWDD_cursor( (_preferences & World::PREF_SOFTMOUSE) != 0 );

        std::string fontStr = _screenSize.x >= 512 ? Locale::Text::Font() : Locale::Text::SmallFont();
        fontStr = System::ResolveMenuFontDescr(fontStr);
        GFX::Engine.LoadFontByDescr(fontStr);
        Gui::UA::LoadFont(fontStr);
    }

    NC_STACK_bitmap *diskScreenImage = loadDisk_screen(this);

    if ( diskScreenImage )
        draw_splashScreen(this, diskScreenImage);


    std::string oldRsrc = Common::Env.SetPrefix("rsrc", "data:fonts");

    int v19 = load_fonts_and_icons();

    Common::Env.SetPrefix("rsrc", oldRsrc);

    if ( !v19 )
        return 0;

    int tod = loadTOD(this, "tod.def");

    int next_tod = tod + 1;

    if ( next_tod + 2490 > 2512 )
        next_tod = 0;

    writeTOD(this, "tod.def", next_tod);

    if ( diskScreenImage )
    {
        drawSplashScreenWithTOD(this, diskScreenImage, Locale::Text::ToD(tod, " "));
        diskScreenImage->Delete();
    }

    _profileFramesCount = 0;
    for (int i = 0; i < PFID_MAX; i++)
    {
        _profileVals[i] = 0;
        _profileMax[i] = 0;
        _profileMin[i] = 100000;
        _profileTotal[i] = 0;
    }

    _history.Clear();
    _historyLastIsTimeStamp = false;

    audio_volume = SFXEngine::SFXe.getMasterVolume();

    _voiceMessage.Reset();

    Common::Env.SetPrefix("rsrc", "data:");

    if ( sub_4DA41C(mapp, _globalMapRegions.MapRegions[_levelInfo.LevelID].MapDirectory) && mapp->IsOk() )
    {
        Common::DeleteAndNull(&_script);

        if (mapp->EventLoopID >= 1 && mapp->EventLoopID <= 3)
        {
            _script = new World::LuaEvents(this);
            _script->LoadFile(fmt::sprintf("lesson%d.lua", mapp->EventLoopID));
            _script->CallInit(_timeStamp);
        }
        else if (!_luaScriptName.empty())
        {
            _script = new World::LuaEvents(this);
            _script->LoadFile(_luaScriptName);
            _script->CallInit(_timeStamp);
        }

        _energyAccumMap.Clear();
        _nextPSForUpdate = 0;

        _inBuildProcess.clear();

        Common::Env.SetPrefix("rsrc", fmt::sprintf("data:set%d", mapp->SetID));

        sb_0x44ca90__sub2(this, mapp);

        if ( yw_LoadSet(mapp->SetID) )
        {
            if ( yw_loadSky(this, mapp->SkyStr) )
                ok = 1;
        }
    }

    FFeedback_Init();

    return ok;
}

bool NC_STACK_ypaworld::LoadTypeMap(const std::string &mapName)
{
    if ( _lvlTypeMap.IsNull() )
        _lvlTypeMap = World::LoadMapDataFromImage(mapName);

    if ( _lvlTypeMap.IsNull() )
        return false;

    SetMapSize(_lvlTypeMap.Size());

    int32_t id = 0;
    for(int y = 0; y < _mapSize.y; y++)
    {
        for (int x = 0; x < _mapSize.x; x++)
        {
            cellArea &cell = _cells(x, y);

            TSectorDesc *sectp = &_secTypeArray[ _lvlTypeMap( cell.CellId ) ];

            cell.type_id = _lvlTypeMap( cell.CellId );
            cell.SectorType = sectp->SectorType;
            cell.energy_power = 0;

            if ( sectp->SectorType == 1)
            {
                cell.buildings_health.At(0, 0) = sectp->SubSectors.At(0, 0)->StartHealth;
            }
            else
            {
                for (int bldY = 0; bldY < 3; bldY++)
                    for (int bldX = 0; bldX < 3; bldX++)
                        cell.buildings_health.At(bldX, bldY) = sectp->SubSectors.At(bldX, bldY)->StartHealth;
            }

            id++;
        }
    }
    return true;
}

bool NC_STACK_ypaworld::LoadOwnerMap(const std::string &mapName)
{
    _countSectorsPerOwner.fill(0);

    if ( _lvlOwnMap.IsNull() )
        _lvlOwnMap = World::LoadMapDataFromImage(mapName);

    if ( _lvlOwnMap.IsNull() )
        return false;

    if ( _lvlOwnMap.Size() != _mapSize )
    {
        ypa_log_out("Mapsize mismatch %s: is [%d,%d], should be [%d,%d].\n", mapName.c_str(), _lvlOwnMap.Width(), _lvlOwnMap.Height(), _mapSize.x, _mapSize.y);
        _lvlOwnMap.Clear();
        return false;
    }


    for (uint32_t yy = 0; yy < _lvlOwnMap.Height(); yy++)
    {
        for (uint32_t xx = 0; xx < _lvlOwnMap.Width(); xx++)
        {
            Common::Point cellId(xx, yy);
            if ( IsGamePlaySector( cellId ) )
            {
                _cells(cellId).owner = _lvlOwnMap(cellId);
                _countSectorsPerOwner[ _lvlOwnMap(cellId) ]++;
            }
            else
            {
                _cells(cellId).owner = 0;
                _countSectorsPerOwner[ 0 ]++;
            }
        }
    }

    return true;
}

bool NC_STACK_ypaworld::LoadHightMap(const std::string &mapName)
{
    if ( _lvlHeightMap.IsNull() )
        _lvlHeightMap = World::LoadMapDataFromImage(mapName);

    if ( _lvlHeightMap.IsNull() )
        return false;

    if ( _lvlHeightMap.Size() != _mapSize )
    {
        ypa_log_out("Mapsize mismatch %s: is [%d,%d], should be [%d,%d].\n", mapName.c_str(), _lvlHeightMap.Width(), _lvlHeightMap.Height(), _mapSize.x, _mapSize.y);
        _lvlHeightMap.Clear();
        return false;
    }

    for (int y = 0; y < _mapSize.y; y++)
    {
        for (int x = 0; x < _mapSize.x; x++)
        {
            cellArea &cell = _cells(x, y);
            cell.height = (-100.0) * _lvlHeightMap( cell.CellId );
            cell.CenterPos = World::SectorIDToCenterPos3( cell.CellId );
            cell.CenterPos.y = cell.height;
        }
    }

    for (int y = 1; y < _mapSize.y; y++)
    {
        for (int x = 1; x < _mapSize.x; x++)
        {
            _cells(x, y).averg_height = (_cells(x    ,     y).height +
                                         _cells(x - 1,     y).height +
                                         _cells(x - 1, y - 1).height +
                                         _cells(x    , y - 1).height ) / 4.0;
        }
    }

    return true;
}

bool NC_STACK_ypaworld::yw_createRobos(const std::vector<MapRobo> &Robos)
{
    if ( _levelInfo.Mode != 1 )
    {
        _levelInfo.OwnerMask = 0;
        int firstOwner = Robos.empty() ? 0 : Robos[0].Owner;
        _levelInfo.UserMask = (firstOwner >= 0 && firstOwner < (int)World::CVFractionsCount) ? (1 << firstOwner) : 0;

        bool first = true;

        for ( const MapRobo &roboInf : Robos)
        {
            ypaworld_arg136 v14;
            v14.stPos = vec3d::X0Z(roboInf.Pos) - vec3d::OY(30000.0);
            v14.vect = vec3d::OY(50000.0);
            v14.flags = 0;

            ypaworld_arg146 v15;
            v15.vehicle_id = roboInf.VhclID;
            v15.pos = roboInf.Pos;

            ypaworld_func136(&v14);

            if ( v14.isect )
                v15.pos.y += v14.isectPos.y;

            NC_STACK_yparobo *robo = dynamic_cast<NC_STACK_yparobo *>( ypaworld_func146(&v15) );

            if ( robo )
            {
                int v20 = 0;

                ypaworld_func134(robo);

                int v12;

                if ( !first )
                {
                    v12 = roboInf.Energy;
                }
                else
                {
                    v12 = roboInf.Energy / 4;

                    if ( v12 < _maxRoboEnergy )
                    {
                        v12 = _maxRoboEnergy;
                        v20 = _maxReloadConst;
                    }

                }

                robo->_owner = roboInf.Owner;
                robo->_energy = v12;
                robo->_energy_max = v12;

                if ( !v20 )
                {
                    if ( roboInf.ReloadConst )
                        v20 = roboInf.ReloadConst;
                    else
                        v20 = robo->_energy_max;
                }

                robo->_reload_const = v20;

                robo->setBACT_bactCollisions(1);
                robo->setROBO_fillMode(15);
                robo->setROBO_battVehicle(v12);
                robo->setROBO_battBeam(v12);

                _levelInfo.OwnerMask |= 1 << roboInf.Owner;

                robo->setROBO_epConquer(roboInf.ConBudget);
                robo->setROBO_epDefense(roboInf.DefBudget);
                robo->setROBO_epRadar(roboInf.RadBudget);
                robo->setROBO_epPower(roboInf.PowBudget);
                robo->setROBO_epSafety(roboInf.SafBudget);
                robo->setROBO_epChangeplace(roboInf.CplBudget);
                robo->setROBO_epRobo(roboInf.RobBudget);
                robo->setROBO_epReconnoitre(roboInf.RecBudget);
                robo->setROBO_viewAngle(roboInf.ViewAngle);
                robo->setROBO_safDelay(roboInf.SafDelay);
                robo->setROBO_powDelay(roboInf.PowDelay);
                robo->setROBO_cplDelay(roboInf.CplDelay);
                robo->setROBO_radDelay(roboInf.RadDelay);
                robo->setROBO_defDelay(roboInf.DefDelay);
                robo->setROBO_conDelay(roboInf.ConDelay);
                robo->setROBO_recDelay(roboInf.RecDelay);
                robo->setROBO_robDelay(roboInf.RobDelay);

                if ( first )
                {
                    robo->setBACT_viewer(true);
                    robo->setBACT_inputting(true);
                }
            }

            first = false;
        }
    }
    return true;
}

bool NC_STACK_ypaworld::LoadBlgMap(const std::string &mapName)
{
    if ( _lvlBuildingsMap.IsNull() )
        _lvlBuildingsMap = World::LoadMapDataFromImage(mapName);

    if ( _lvlBuildingsMap.IsNull() )
        return false;

    if ( _lvlBuildingsMap.Size() != _mapSize )
    {
        ypa_log_out("Mapsize mismatch %s: is [%d,%d], should be [%d,%d].\n", mapName.c_str(), _lvlBuildingsMap.Width(), _lvlBuildingsMap.Height(), _mapSize.x, _mapSize.y);
        _lvlBuildingsMap.Clear();
        return false;
    }

    for ( int y = 0; y < _mapSize.y; y++)
    {
        for ( int x = 0; x < _mapSize.x; x++)
        {
            int blg = _lvlBuildingsMap(x, y);
            cellArea &cell = _cells(x, y);

            if (blg && cell.owner)
            {
                ypaworld_arg148 arg148;

                arg148.owner = cell.owner;
                arg148.blg_ID = blg;
                arg148.field_C = 1;
                arg148.field_18 = 0;
                arg148.CellId = cell.CellId;

                ypaworld_func148(&arg148);
            }
        }
    }

    return true;
}

void NC_STACK_ypaworld::yw_InitSquads(const std::vector<MapSquad> &squads)
{
    if ( _levelInfo.Mode != 1 )
    {
        size_t i = 0;
        for ( const MapSquad &squad : squads )
        {
            NC_STACK_yparobo *robo = NULL;

            for( NC_STACK_ypabact *unit : _unitsList )
            {
                if ( unit->_bact_type == BACT_TYPES_ROBO && unit->_owner == squad.Owner)
                {
                    robo = dynamic_cast<NC_STACK_yparobo *>(unit);
                    break;
                }
            }

            if ( !robo )
            {
                ypa_log_out("WARNING: yw_InitSquads(): no host robo for squad[%d], owner %d!\n", i, squad.Owner);
            }
            else
            {
                vec3d squadPos;

                ypaworld_arg136 arg136;
                arg136.stPos.x = squad.X;
                arg136.stPos.y = -50000.0;
                arg136.stPos.z = squad.Z;
                arg136.vect = vec3d::OY(100000.0);
                arg136.flags = 0;
                ypaworld_func136(&arg136);

                if ( arg136.isect )
                    squadPos = arg136.isectPos - vec3d::OY(50.0);
                else
                {
                    yw_130arg sect_info;
                    sect_info.pos_x = squad.X;
                    sect_info.pos_z = squad.Z;

                    if ( !GetSectorInfo(&sect_info) )
                    {
                        ypa_log_out("yw_InitSquads(): no valid position for squad[%d]!\n", i);
                        return;
                    }

                    squadPos.x = squad.X;
                    squadPos.y = sect_info.pcell->height;
                    squadPos.z = squad.Z;
                }
                // Create squad by robo method
                robo->MakeSquad( std::vector<int>(squad.Count, squad.VhclID), squadPos, squad.Useable); // yparobo_func133
            }

            i++;
        }
    }
}

bool NC_STACK_ypaworld::IsSpectatorModeEnabled() const
{
    return System::IniConf::GameSpectatorMode.Get<bool>() &&
           System::IniConf::GameSpectatorVehicleID.Get<int32_t>() > 0;
}

bool NC_STACK_ypaworld::IsSpectatorVehicleID(int vehicleID) const
{
    return IsSpectatorModeEnabled() &&
           vehicleID == System::IniConf::GameSpectatorVehicleID.Get<int32_t>();
}

bool NC_STACK_ypaworld::IsSpectatorBact(const NC_STACK_ypabact *bact) const
{
    return bact && IsSpectatorVehicleID(bact->_vehicleID);
}

bool NC_STACK_ypaworld::IsSpectatorControlled() const
{
    return IsSpectatorBact(_userUnit);
}

bool NC_STACK_ypaworld::CanControlUnitInSpectatorMode(const NC_STACK_ypabact *bact) const
{
    if ( !IsSpectatorControlled() )
        return true;

    return IsSpectatorBact(bact);
}

static bool yw_IsBactInSpectatorFollowTree(NC_STACK_ypabact *node, NC_STACK_ypabact *target)
{
    if ( !node )
        return false;

    if ( node == target )
        return true;

    for ( NC_STACK_ypabact *kid : node->_kidList )
    {
        if ( yw_IsBactInSpectatorFollowTree(kid, target) )
            return true;
    }

    return false;
}

bool NC_STACK_ypaworld::IsSpectatorFollowActive() const
{
    return _spectatorFollowTarget != NULL;
}

NC_STACK_ypabact *NC_STACK_ypaworld::GetSpectatorFollowTarget() const
{
    return _spectatorFollowTarget;
}

bool NC_STACK_ypaworld::IsValidSpectatorFollowTarget(NC_STACK_ypabact *bact) const
{
    if ( !IsSpectatorControlled() || !bact )
        return false;

    bool found = false;
    for ( NC_STACK_ypabact *unit : _unitsList )
    {
        if ( yw_IsBactInSpectatorFollowTree(unit, bact) )
        {
            found = true;
            break;
        }
    }

    if ( !found )
        return false;

    return bact->_owner != World::OWNER_0 &&
           !IsSpectatorBact(bact) &&
           bact->_status != BACT_STATUS_DEAD &&
           bact->_status != BACT_STATUS_CREATE &&
           bact->_status != BACT_STATUS_BEAM &&
           bact->_bact_type != BACT_TYPES_MISSLE &&
           !bact->ShouldHideFromStrategicUI();
}

void NC_STACK_ypaworld::SetSpectatorFollowTarget(NC_STACK_ypabact *bact)
{
    if ( !IsValidSpectatorFollowTarget(bact) )
        return;

    _spectatorFollowTarget = bact;
    _viewerBact = bact;
    _spectatorFollowDistance = 900.0;
    _spectatorFollowTargetDistance = _spectatorFollowDistance;
    _spectatorFollowPitch = 0.25;
    _spectatorFollowYaw = atan2(-bact->_rotation.m20, bact->_rotation.m22);

    if ( _userUnit && IsSpectatorBact(_userUnit) )
        _userUnit->setBACT_inputting(false);

    _mouseGrabbed = false;
}

void NC_STACK_ypaworld::ClearSpectatorFollowTarget()
{
    _spectatorFollowTarget = NULL;
}

void NC_STACK_ypaworld::ReturnToSpectatorVehicle()
{
    ClearSpectatorFollowTarget();

    if ( _userUnit && IsSpectatorBact(_userUnit) )
    {
        _viewerBact = _userUnit;
        _userUnit->setBACT_viewer(true);
        _userUnit->setBACT_inputting(true);
    }

    _mouseGrabbed = false;
}

bool NC_STACK_ypaworld::UpdateSpectatorFollowCamera(TInputState *inpt)
{
    if ( !IsSpectatorFollowActive() )
        return false;

    if ( !IsValidSpectatorFollowTarget(_spectatorFollowTarget) )
    {
        ReturnToSpectatorVehicle();
        return false;
    }

    // OpenUA custom: Spectator Follow camera uses one tank-style framing rule
    // for every vehicle class.  The old code treated non-ground units with a
    // higher/stranger focus, which made planes, helicopters and Host Stations
    // look inconsistent or clip the camera into large models.  Use the same
    // low, readable tank-style orbit for all targets, then scale distance by
    // the actor's scripted collision/view extents so very large ROBO/Host
    // Stations keep the camera outside their model.
    float targetRadius = _spectatorFollowTarget->_radius;
    if ( _spectatorFollowTarget->_viewer_radius > targetRadius )
        targetRadius = _spectatorFollowTarget->_viewer_radius;
    if ( targetRadius < 80.0f )
        targetRadius = 80.0f;

    float targetExtent = targetRadius;
    if ( fabs(_spectatorFollowTarget->_overeof) > targetExtent )
        targetExtent = fabs(_spectatorFollowTarget->_overeof);
    if ( fabs(_spectatorFollowTarget->_viewer_overeof) > targetExtent )
        targetExtent = fabs(_spectatorFollowTarget->_viewer_overeof);
    if ( fabs(_spectatorFollowTarget->_height) * 0.35f > targetExtent )
        targetExtent = fabs(_spectatorFollowTarget->_height) * 0.35f;
    if ( targetExtent < 80.0f )
        targetExtent = 80.0f;

    bool followTargetIsRobo = _spectatorFollowTarget->_bact_type == BACT_TYPES_ROBO;

    float followMinDistance = targetExtent * 1.65f;
    if ( followMinDistance < 220.0f )
        followMinDistance = 220.0f;

    float followMaxDistance = followMinDistance * 3.0f;
    if ( followMaxDistance < 1300.0f )
        followMaxDistance = 1300.0f;

    if ( followTargetIsRobo )
    {
        // Host Stations/ROBO use very large vertical/viewer extents, so using
        // targetExtent for zoom-in keeps the camera stuck too far away.  Use
        // the horizontal radius for the close limit and keep the larger extent
        // only for focus/terrain safety.  This allows a real zoom-in while
        // still avoiding the obvious inside-model view.
        followMinDistance = targetRadius * 0.55f;
        if ( followMinDistance < 520.0f )
            followMinDistance = 520.0f;
        if ( followMinDistance > 700.0f )
            followMinDistance = 700.0f;

        // Use the same zoom-out logic as normal vehicles after the
        // ROBO close-zoom override.  The previous ROBO minimum max-distance
        // was intentionally safe but too far away, making zoom-out excessive.
        followMaxDistance = followMinDistance * 3.0f;
        if ( followMaxDistance < 1300.0f )
            followMaxDistance = 1300.0f;
    }

    if ( followMaxDistance > 3600.0f )
        followMaxDistance = 3600.0f;
    if ( followMinDistance > followMaxDistance )
        followMinDistance = followMaxDistance;

    float wheelStep = 180.0f;
    if ( targetExtent > 160.0f )
        wheelStep += (targetExtent - 160.0f) * 0.35f;
    if ( wheelStep > 520.0f )
        wheelStep = 520.0f;

    if ( inpt )
    {
        const float fperiod = inpt->Period / 1000.0;

        if ( _mouseGrabbed )
        {
            float yawInput = inpt->Sliders[10];
            float pitchInput = inpt->Sliders[11];

            if ( fabs(yawInput) < 0.01 )
                yawInput = 0.0;

            if ( fabs(pitchInput) < 0.025 )
                pitchInput = 0.0;

            _spectatorFollowYaw -= yawInput * 1.8 * fperiod;
            _spectatorFollowPitch += pitchInput * 1.35 * fperiod;

            if ( _spectatorFollowPitch < -0.75 )
                _spectatorFollowPitch = -0.75;
            else if ( _spectatorFollowPitch > 0.95 )
                _spectatorFollowPitch = 0.95;
        }

        if ( inpt->ClickInf.wheel )
        {
            _spectatorFollowTargetDistance -= inpt->ClickInf.wheel * wheelStep;

            // Spectator Follow should never zoom out far enough to expose the
            // finite UA sky dome/skybox. Keep the camera cinematic and local,
            // but scale the useful range with large vehicles so ROBO/Host
            // Stations cannot swallow the camera.
            if ( _spectatorFollowTargetDistance < followMinDistance )
                _spectatorFollowTargetDistance = followMinDistance;
            else if ( _spectatorFollowTargetDistance > followMaxDistance )
                _spectatorFollowTargetDistance = followMaxDistance;
        }

        if ( fperiod > 0.0 )
        {
            float zoomBlend = 1.0 - exp(-fperiod * 12.0);
            _spectatorFollowDistance += (_spectatorFollowTargetDistance - _spectatorFollowDistance) * zoomBlend;
        }
    }

    if ( _spectatorFollowTargetDistance < followMinDistance )
        _spectatorFollowTargetDistance = followMinDistance;
    else if ( _spectatorFollowTargetDistance > followMaxDistance )
        _spectatorFollowTargetDistance = followMaxDistance;

    if ( _spectatorFollowDistance < followMinDistance )
        _spectatorFollowDistance = followMinDistance;
    if ( _spectatorFollowDistance > followMaxDistance )
        _spectatorFollowDistance = followMaxDistance;

    // Tank-style camera for every class: never force non-ground units into an
    // excessively high/top-down view. This keeps spectator follow homogeneous.
    bool followTargetIsGround = true;
    if ( _spectatorFollowDistance < 1200.0 && _spectatorFollowPitch > 0.55 )
        _spectatorFollowPitch = 0.55;

    float cp = cos(_spectatorFollowPitch);
    float sp = sin(_spectatorFollowPitch);
    float sy = sin(_spectatorFollowYaw);
    float cy = cos(_spectatorFollowYaw);

    vec3d orbitForward(-sy * cp, -sp, cy * cp);
    if ( orbitForward.length() <= 0.001 )
        orbitForward = vec3d(0.0, 0.0, 1.0);
    else
        orbitForward = vec3d::Normalise(orbitForward);

    float targetHeight = targetExtent * 0.22f;
    if ( targetHeight < 45.0f )
        targetHeight = 45.0f;
    if ( targetHeight > 110.0f )
        targetHeight = 110.0f;

    if ( followTargetIsRobo )
    {
        targetHeight = targetExtent * 0.30f;
        if ( targetHeight < 140.0f )
            targetHeight = 140.0f;
        if ( targetHeight > 360.0f )
            targetHeight = 360.0f;
    }

    vec3d focus = _spectatorFollowTarget->_position + vec3d::OY(targetHeight);
    vec3d camPos = focus - orbitForward * _spectatorFollowDistance;

    // Keep the camera out of the terrain. A single final-position clamp is
    // not enough on steep ground: the camera ray can cross a higher sector
    // between the followed unit and the final camera position, especially for
    // ground units.  In UA coordinates smaller Y is higher, so "raising" the
    // camera means subtracting from camPos.y.
    float terrainSafeMargin = 140.0;
    int terrainSamples = 5;

    if ( followTargetIsGround )
    {
        if ( _spectatorFollowDistance < 900.0 )
        {
            terrainSafeMargin = 80.0;
            terrainSamples = 3;
        }
        else if ( _spectatorFollowDistance < 1400.0 )
        {
            terrainSafeMargin = 105.0;
            terrainSamples = 4;
        }
    }

    float raiseCamera = 0.0;

    for ( int i = 1; i <= terrainSamples; ++i )
    {
        float t = (float)i / (float)terrainSamples;
        vec3d sample = focus + (camPos - focus) * t;

        yw_130arg sectInfo;
        sectInfo.pos_x = sample.x;
        sectInfo.pos_z = sample.z;

        if ( GetSectorInfo(&sectInfo) && sectInfo.pcell )
        {
            float safeY = sectInfo.pcell->height - terrainSafeMargin;

            if ( sample.y > safeY )
            {
                float needed = (sample.y - safeY) / t;
                if ( needed > raiseCamera )
                    raiseCamera = needed;
            }
        }
    }

    if ( raiseCamera > 0.0 )
        camPos.y -= raiseCamera;

    // Rebuild the view matrix from the final camera position to the focus point
    // with world-down locked as the camera vertical reference. This keeps the
    // Spectator Follow camera roll-free, so horizontal mouse look no longer
    // banks/tilts the whole horizon.
    vec3d forward = focus - camPos;
    if ( forward.length() <= 0.001 )
        forward = orbitForward;
    else
        forward = vec3d::Normalise(forward);

    vec3d right = vec3d::OY(1.0) * forward;
    if ( right.length() <= 0.001 )
        right = vec3d(1.0, 0.0, 0.0);
    else
        right = vec3d::Normalise(right);

    vec3d down = forward * right;
    if ( down.length() <= 0.001 )
        down = vec3d::OY(1.0);
    else
        down = vec3d::Normalise(down);

    mat3x3 rot = mat3x3::Basis(right, down, forward);

    _viewerBact = _spectatorFollowTarget;
    _viewerPosition = camPos;
    _viewerRotation = rot;
    _spectatorFollowView.Pos = camPos;
    _spectatorFollowView.SclRot = rot;
    _spectatorFollowView.Scale = vec3d(1.0, 1.0, 1.0);
    _spectatorFollowView.Parent = NULL;
    _spectatorFollowView.flags = 0;

    GFX::Engine.matrixAspectCorrection(_spectatorFollowView.SclRot, false);
    TF::Engine.SetViewPoint(&_spectatorFollowView);

    return true;
}

static bool yw_IsUsableSpectatorBact(NC_STACK_ypaworld *yw, NC_STACK_ypabact *unit)
{
    return unit &&
           yw &&
           yw->IsSpectatorBact(unit) &&
           unit->_status != BACT_STATUS_DEAD &&
           unit->_status != BACT_STATUS_CREATE &&
           unit->_status != BACT_STATUS_BEAM &&
           unit->_energy > 0 &&
           !(unit->_status_flg & (BACT_STFLAG_DEATH1 | BACT_STFLAG_DEATH2));
}

static NC_STACK_ypabact *yw_FindSpectatorBactInList(NC_STACK_ypaworld *yw, World::RefBactList &list)
{
    for (NC_STACK_ypabact *unit : list)
    {
        if ( yw_IsUsableSpectatorBact(yw, unit) )
            return unit;

        if ( NC_STACK_ypabact *kid = yw_FindSpectatorBactInList(yw, unit->_kidList) )
            return kid;
    }

    return NULL;
}

static void yw_ApplySpectatorObserverSafety(NC_STACK_ypabact *spectator)
{
    if ( !spectator )
        return;

    // Runtime-only neutralization: the spectator is an observer, not an
    // owner-1 combat unit. Owner 1 keeps the LDF-loaded setup, while the
    // observer itself is neutral/intangible.
    spectator->_owner = World::OWNER_0;
    spectator->_m_owner = World::OWNER_0;
    spectator->_killer = NULL;
    spectator->_killer_owner = World::OWNER_0;
    spectator->_host_station = NULL;
    spectator->_weapon = -1;
    spectator->_mgun = -1;
    spectator->_weapon_flags = 0;
    spectator->_radar = 0;
    spectator->_unhideRadar = 0;
    spectator->_status_flg |= BACT_STFLAG_NOMSG;
    spectator->setBACT_bactCollisions(false);
    spectator->setBACT_exactCollisions(false);
}

static float yw_ClampSpectatorCoord(float value, float minValue, float maxValue)
{
    if ( maxValue < minValue )
        return value;

    if ( value < minValue )
        return minValue;

    if ( value > maxValue )
        return maxValue;

    return value;
}

static vec3d yw_GetSpectatorSpawnPosition(NC_STACK_ypaworld *yw)
{
    Common::Point cell(1, 1);
    if ( yw->_mapSize.x > 2 && yw->_mapSize.y > 2 )
        cell = Common::Point(yw->_mapSize.x / 2, yw->_mapSize.y / 2);

    // Spectator Mode is an observer mode, not owner-1 gameplay. Spawn the
    // observer in the map center instead of near the player Host Station.
    vec3d pos = World::SectorIDToCenterPos3(cell);

    const float halfSector = World::CVSectorHalfLength;
    pos.x = yw_ClampSpectatorCoord(pos.x, halfSector, yw->_mapLength.x - halfSector);
    pos.z = yw_ClampSpectatorCoord(pos.z, -yw->_mapLength.y + halfSector, -halfSector);

    ypaworld_arg136 ray;
    ray.stPos = vec3d(pos.x, -50000.0, pos.z);
    ray.vect = vec3d::OY(100000.0);
    ray.flags = 0;

    yw->ypaworld_func136(&ray);

    if ( ray.isect )
    {
        pos.y = ray.isectPos.y - 50.0;
    }
    else
    {
        yw_130arg sectInfo;
        sectInfo.pos_x = pos.x;
        sectInfo.pos_z = pos.z;

        if ( yw->GetSectorInfo(&sectInfo) )
            pos.y = sectInfo.pcell->height - 50.0;
    }

    return pos;
}

void NC_STACK_ypaworld::TryActivateSpectatorMode()
{
    if ( !System::IniConf::GameSpectatorMode.Get<bool>() || _levelInfo.Mode == 1 )
        return;

    int spectatorVehicleID = System::IniConf::GameSpectatorVehicleID.Get<int32_t>();
    if ( spectatorVehicleID <= 0 || spectatorVehicleID >= (int)_vhclProtos.size() )
    {
        ypa_log_out("WARNING: game.spectator_vehicle_id %d is invalid; spectator mode disabled for this level.\n", spectatorVehicleID);
        return;
    }

    NC_STACK_ypabact *spectator = yw_FindSpectatorBactInList(this, _unitsList);

    if ( !spectator )
    {
        ypaworld_arg146 arg146;
        arg146.vehicle_id = spectatorVehicleID;
        arg146.pos = yw_GetSpectatorSpawnPosition(this);

        spectator = ypaworld_func146(&arg146);
        if ( !spectator )
        {
            ypa_log_out("WARNING: could not create spectator vehicle id %d.\n", spectatorVehicleID);
            return;
        }

        ypaworld_func134(spectator);
    }

    yw_ApplySpectatorObserverSafety(spectator);

    if ( _viewerBact && _viewerBact != spectator )
        _viewerBact->setBACT_viewer(false);

    if ( _userUnit && _userUnit != spectator )
        _userUnit->setBACT_inputting(false);

    spectator->setBACT_viewer(true);
    spectator->setBACT_inputting(true);

    // Spectator mode must not start with owner-1 squads pre-selected. The
    // original player faction is simulated by AI only; direct player command
    // selection remains disabled while the spectator is controlled.
    _activeCmdrID = 0;
    _activeCmdrRemapIndex = -1;
    _activeCmdrKidsCount = 0;
    _cmdrIdToSelect = -1;
    _cmdrsRemap.clear();
}

void NC_STACK_ypaworld::InitBuddies()
{
    if ( !_levelInfo.Buddies.empty() )
    {
        int squad_sn = 0;

        std::vector<TMapBuddy> buds = _levelInfo.Buddies;
        while ( 1 )
        {
            std::vector<int> VhclIDS;
            int wrkID = -1;
            for (std::vector<TMapBuddy>::iterator it = buds.begin(); it != buds.end(); )
            {
                if (wrkID == -1 || wrkID == it->CommandID )
                {
                    wrkID = it->CommandID;
                    VhclIDS.push_back(it->Type);
                    it = buds.erase(it);
                }
                else
                    it++;
            }

            if ( wrkID == -1 )
                break;

            vec3d squadPos =    _userRobo->_position +
                                vec3d(  sin(squad_sn * 1.745) * 500.0,
                                        0.0,
                                        cos(squad_sn * 1.745) * 500.0 );

            ypaworld_arg136 arg136;
            arg136.stPos = squadPos.X0Z() + vec3d(0.5, -50000.0, 0.75);
            arg136.vect = vec3d::OY(100000.0);
            arg136.flags = 0;

            ypaworld_func136(&arg136);

            if ( arg136.isect )
                squadPos.y = arg136.isectPos.y + -100.0;

            NC_STACK_yparobo *robo = dynamic_cast<NC_STACK_yparobo *>(_userRobo);

            robo->MakeSquad(VhclIDS, squadPos, true); //robo 133 method

            squad_sn++;
        }
    }
}

void NC_STACK_ypaworld::yw_InitTechUpgradeBuildings()
{
    _upgradeTimeStamp = 0;
    _upgradeBuildId = 0;
    _upgradeVehicleId = 0;
    _upgradeWeaponId = 0;
    _upgradeId = -1;
    ClearGemNotificationCapture();
    _gemUnlockSoundAttemptedPath.clear();

    for (size_t i = 0; i < _techUpgrades.size(); i++)
    {
        TMapGem &gem = _techUpgrades[i];
        cellArea &cell = _cells(gem.CellId);

        if (gem.BuildingID)
        {
            if ( cell.PurposeType != cellArea::PT_BUILDINGS || gem.BuildingID != cell.PurposeIndex )
            {
                ypaworld_arg148 arg148;
                arg148.owner = cell.owner;
                arg148.blg_ID = gem.BuildingID;
                arg148.CellId = gem.CellId;
                arg148.field_C = 1;
                arg148.field_18 = 0;

                ypaworld_func148(&arg148);
            }
        }

        cell.PurposeType = cellArea::PT_TECHUPGRADE;
        cell.PurposeIndex = i;
    }
}

void NC_STACK_ypaworld::InitGates()
{
    for (size_t i = 0; i < _levelInfo.Gates.size(); i++)
    {
        TMapGate &gate = _levelInfo.Gates[i];

        gate.PCell = &_cells(gate.CellId);

        ypaworld_arg148 arg148;
        arg148.owner = gate.PCell->owner;
        arg148.blg_ID = gate.ClosedBldID;
        arg148.field_C = 1;
        arg148.CellId = gate.CellId;
        arg148.field_18 = 0;

        ypaworld_func148(&arg148);

        gate.PCell->PurposeType = cellArea::PT_GATECLOSED;
        gate.PCell->PurposeIndex = i;

        for (TMapKeySector &ks : gate.KeySectors)
        {
            if ( IsGamePlaySector( ks.CellId ) )
            {
                ks.PCell = &_cells(ks.CellId);
            }
        }
    }
}

void NC_STACK_ypaworld::InitSuperItems()
{
    for ( size_t i = 0; i < _levelInfo.SuperItems.size(); i++ )
    {
        TMapSuperItem &sitem = _levelInfo.SuperItems[i];

        sitem.PCell = &_cells(sitem.CellId);

        ypaworld_arg148 arg148;
        arg148.owner = sitem.PCell->owner;
        arg148.blg_ID = sitem.InactiveBldID;
        arg148.field_C = 1;
        arg148.CellId = sitem.CellId;
        arg148.field_18 = 0;

        ypaworld_func148(&arg148);

        sitem.PCell->PurposeType = cellArea::PT_STOUDSON;
        sitem.PCell->PurposeIndex = i;

        for ( TMapKeySector &ks : sitem.KeySectors )
        {
            if ( IsGamePlaySector( ks.CellId ) )
                ks.PCell = &_cells(ks.CellId);
        }

        sitem.ActiveTime = 0;
        sitem.TriggerTime = 0;
        sitem.ActivateOwner = 0;
        sitem.State = TMapSuperItem::STATE_INACTIVE;
    }
}

void NC_STACK_ypaworld::UpdatePowerEnergy()
{
    // Apply power to sectors and clean power matrix for next compute iteration.

    for (int y = 0; y < _mapSize.y; y++)
    {
        for (int x = 0; x < _mapSize.x; x++)
        {
            cellArea &cell = _cells(x, y);
            EnergyAccum &accum = _energyAccumMap(x, y);

            accum.Owner = cell.owner;
            cell.energy_power = accum.Energy; // Apply power to cell
            accum.Energy = 0; // Clean matrix's power
        }
    }

    _nextPSForUpdate = 0; // Next power station for recompute power is first
}


void NC_STACK_ypaworld::CellSetOwner(cellArea *cell, uint8_t owner)
{
    if ( cell->owner != owner )
    {
        uint8_t oldOwner = cell->owner;

        HistoryEventAdd( World::History::Conq(cell->CellId.x, cell->CellId.y, owner) );

        if ( cell->PurposeType == cellArea::PT_POWERSTATION )
            HistoryEventAdd( World::History::PowerST(cell->CellId.x, cell->CellId.y, owner) );

        _countSectorsPerOwner[cell->owner]--;
        _countSectorsPerOwner[owner]++;

        cell->owner = owner;

        // Building spawners belong to the original owner of the cell.
        // When the building/sector is conquered, stop the previous spawner state
        // and do not let the new owner inherit that spawner.
        if ( cell->BuildingSpawnInitialOwner == 0 )
            cell->BuildingSpawnInitialOwner = oldOwner;

        cell->BuildingSpawnLastTime = 0;
        cell->BuildingSpawnedGids.clear();
    }
}

void NC_STACK_ypaworld::CellSetNewOwner(cellArea *cell, NC_STACK_ypabact *a5, int newOwner)
{
    int energon[World::FRACTION_MAXCOUNT];

    if ( newOwner < World::OWNER_UNKNOW )
    {
        newOwner = cell->owner;

        for( int &e : energon )
            e = 0;

        for ( NC_STACK_ypabact* &nod : cell->unitsList )
            energon[nod->_owner] += nod->_energy;

        energon[0] = 0;

        for (int i = 0; i < World::FRACTION_MAXCOUNT; i++)
        {
            if ( energon[i] > energon[newOwner] )
                newOwner = i;
        }
    }

    if ( cell->owner != newOwner )
    {
        if ( cell->PurposeType == cellArea::PT_POWERSTATION )
        {
            if ( _userRobo->_owner == newOwner )
            {
                if ( a5 )
                {
                    yw_arg159 v21;
                    v21.unit = a5;
                    v21.Priority = 78;
                    v21.MsgID = 45;

                    ypaworld_func159(&v21);
                }
            }
            else if ( cell->owner == _userRobo->_owner )
            {
                yw_arg159 v24;
                v24.unit = NULL;
                v24.Priority = 78;
                v24.MsgID = 67;

                ypaworld_func159(&v24);
            }
        }
        else
        {
            for ( const TMapGate &gate : _levelInfo.Gates )
            {
                for ( const TMapKeySector &ks : gate.KeySectors )
                {
                    if ( cell == ks.PCell )
                    {
                        if ( _userRobo->_owner == newOwner )
                        {
                            yw_arg159 v23;
                            v23.unit = NULL;
                            v23.Priority = 80;
                            v23.MsgID = 82;

                            ypaworld_func159(&v23);
                        }
                        else if ( _userRobo->_owner == cell->owner )
                        {
                            yw_arg159 v22;
                            v22.unit = NULL;
                            v22.Priority = 80;
                            v22.MsgID = 81;

                            ypaworld_func159(&v22);
                        }
                    }
                }
            }
        }
    }

    CellSetOwner(cell, newOwner);
}

void NC_STACK_ypaworld::CellCheckHealth(cellArea *cell, int newOwner, NC_STACK_ypabact *a6)
{
    if ( cell->IsGamePlaySector() )
    {
        int helth = 0;

        for (auto h : cell->buildings_health)
            helth += h;

        if ( cell->PurposeType == cellArea::PT_POWERSTATION )
        {
            if ( helth )
            {
                auto psIt = _powerStations.find(cell->Id);
                if (psIt != _powerStations.end())
                {
                    TPowerStationInfo &psInf = psIt->second;
                    int effPower = (helth * psInf.Power) / 255 ;

                    if ( effPower < 0 )
                        effPower = 0;
                    else if ( effPower > 255 )
                        effPower = 255;

                    psInf.EffectivePower = effPower;
                }
            }
            else
            {
                PowerStationErase(cell);
            }
        }

        /* Fix for vanilla game bug that recalc sector owner after loading
         * saved game. */
        if (newOwner != World::OWNER_NOCHANGE)
        {
            if ( cell->SectorType == 1 )
            {
                if ( helth < 224 )
                    CellSetNewOwner(cell, a6, newOwner);
            }
            else if ( helth < 1728 )
            {
                CellSetNewOwner(cell, a6, newOwner);
            }
        }
    }
    else
    {
        CellSetOwner(cell, 0);
    }
}



TSectorCollision NC_STACK_ypaworld::sub_44DBF8(int _dx, int _dz, int _dxx, int _dzz, int flags)
{
    int v8 = flags;

    TSectorCollision tmp;
    tmp.sklt = NULL;
    tmp.Flags = 0;
    tmp.CollisionType = 0;



    if ( _dxx > 0 && _dxx < 4 * _mapSize.x - 1 && _dzz > 0  &&  _dzz < 4 * _mapSize.y - 1)
    {
        tmp.Cell = Common::Point(_dxx / 4, _dzz / 4);

        cellArea &cell = _cells(tmp.Cell);

        if ( _dxx % 4 && _dzz % 4 )
        {
            tmp.CollisionType = 1;

            int v14, v16;

            if ( cell.SectorType == 1 )
            {
                v14 = 0;
                v16 = 0;

                if ( (_dxx / 4) == (_dx / 4)  &&  (_dz / 4) == (_dzz / 4) )
                    v8 = flags & ~1;

                tmp.pos = World::SectorIDToCenterPos3(tmp.Cell);
                tmp.pos.y = cell.height;
            }
            else
            {
                v16 = (_dxx % 4) - 1;
                v14 = 2 - ((_dzz % 4) - 1);

                if ( _dxx == _dx && _dzz == _dz )
                    v8 = flags & ~1;

                tmp.pos.z = -(_dzz * 300.0);
                tmp.pos.x = _dxx * 300.0;
                tmp.pos.y = cell.height;
            }

            tmp.Flags = v8;

            int model_id = GetLegoBld(&cell, v16, v14);

            if ( v8 & 1 )
                tmp.sklt = _legoArray[model_id].UseCollisionSkelet;
            else
                tmp.sklt = _legoArray[model_id].CollisionSkelet;
        }
        else
        {
            tmp.pos.y = 0;
            tmp.pos.x = _dxx * 300.0;
            tmp.pos.z = -(_dzz * 300.0);

            if ( _dxx == _dx && _dzz == _dz )
                v8 = flags & ~1;

            tmp.Flags = v8;

            if ( _dxx % 4 == 0 && _dzz % 4 == 0)
            {
                tmp.sklt = _fillerCross;
                tmp.CollisionType = 4;
            }
            else if ( _dxx % 4 == 0 && _dzz % 4 != 0 )
            {
                tmp.sklt = _fillerSide;
                tmp.CollisionType = 2;
            }
            else if ( _dxx % 4 != 0 && _dzz % 4 == 0 )
            {
                tmp.sklt = _fillerSide;
                tmp.CollisionType = 3;
            }
        }

        if ( tmp.CollisionType && !tmp.sklt )
        {
            ypa_log_out("yw_GetSklt: WARNING, not CZT_INVALID, but Sklt NULL!\n");

            const char *v17 = "UNKNOWN";

            switch ( tmp.CollisionType )
            {
            case 4:
                v17 = "czt_cross_slurp";
                break;
            case 2:
                v17 = "czt_vside_slurp";
                break;
            case 3:
                v17 = "czt_hside_slurp";
                break;
            case 1:
                v17 = "czt_lego";
                break;
            default:
                break;
            }
            ypa_log_out("    Type=%s, sec_x=%d, sec_y=%d.\n", v17, tmp.Cell.x, tmp.Cell.y);

            tmp.CollisionType = 0;
        }
    }

    return tmp;
}

void sub_44DF60(UAskeleton::Data *arg, int id)
{
    UAskeleton::Polygon &tr = arg->polygons[id];

    int vtx1 = arg->polygons[id].v[0];
    int vtx2 = arg->polygons[id].v[1];
    int vtx3 = arg->polygons[id].v[2];

    vec3d tmp  = arg->POO[vtx2] - arg->POO[vtx1];
    vec3d tmp2 = arg->POO[vtx3] - arg->POO[vtx2];

    vec3d tmp3 = tmp * tmp2;
    tmp3.normalise();

    tr.A = tmp3.x;
    tr.B = tmp3.y;
    tr.C = tmp3.z;

    tr.D = -tmp3.dot( arg->POO[vtx1] );
}

void NC_STACK_ypaworld::sub_44E07C(TSectorCollision &arg)
{
    if ( arg.CollisionType == 2 )
    {
        cellArea &cur = _cells(arg.Cell);
        cellArea &left = _cells(arg.Cell + Common::Point(-1, 0));

        if ( !(arg.Flags & 1) || fabs( (int)(cur.height - left.height)) < 500.0 )
        {
            UAskeleton::Data *skel = arg.sklt->GetSkelet();
            skel->POO[0].y = left.height;
            skel->POO[1].y = cur.height;
            skel->POO[2].y = cur.height;
            skel->POO[3].y = left.height;

            sub_44DF60(skel, 0);
        }
        else
        {
            arg.sklt = _colsubSkeleton;

            if ( cur.height > left.height )
                arg.pos.y = cur.height;
            else
                arg.pos.y = left.height;
        }
    }
    else if ( arg.CollisionType == 3 )
    {
        cellArea &cur = _cells(arg.Cell);
        cellArea &up = _cells(arg.Cell + Common::Point(0, -1));

        if ( !(arg.Flags & 1) || fabs( (int)(cur.height - up.height)) < 500.0 )
        {
            UAskeleton::Data *skel = arg.sklt->GetSkelet();
            skel->POO[0].y = up.height;
            skel->POO[1].y = up.height;
            skel->POO[2].y = cur.height;
            skel->POO[3].y = cur.height;

            sub_44DF60(skel, 0);
        }
        else
        {
            arg.sklt = _colsubSkeleton;

            if ( cur.height > up.height )
                arg.pos.y = cur.height;
            else
                arg.pos.y = up.height;
        }
    }
    else if ( arg.CollisionType == 4 )
    {

        int kk = 0;

        cellArea &cur = _cells(arg.Cell);
        cellArea &left = _cells(arg.Cell + Common::Point(-1, 0));
        cellArea &up = _cells(arg.Cell + Common::Point(0, -1));
        cellArea &leftup = _cells(arg.Cell + Common::Point(-1, -1));

        if ( arg.Flags & 1 )
        {
            float cs = cur.height;
            float ls = left.height;
            float us = up.height;
            float lus = leftup.height;

            float v15, v16, v17, v18;

            if ( cs >= ls )
                v15 = ls;
            else
                v15 = cs;

            if ( us >= lus )
                v16 = lus;
            else
                v16 = us;

            if ( v15 < v16 )
                v16 = v15;

            if ( cs <= ls )
                v17 = ls;
            else
                v17 = cs;

            if ( us <= lus )
                v18 = lus;
            else
                v18 = us;

            if ( v17 > v18 )
                v18 = v17;

            if ( fabs( (int)(v18 - v16) ) > 300.0 )
            {
                arg.sklt = _colsubSkeleton;
                arg.pos.y = v18;
                kk = 1;
            }
        }
        if ( !kk )
        {
            UAskeleton::Data *skel = arg.sklt->GetSkelet();
            skel->POO[0].y = leftup.height;
            skel->POO[1].y = up.height;
            skel->POO[2].y = cur.height;
            skel->POO[3].y = left.height;
            skel->POO[4].y = cur.averg_height;

            sub_44DF60(skel, 0);
            sub_44DF60(skel, 1);
            sub_44DF60(skel, 2);
            sub_44DF60(skel, 3);
        }
    }
}

int sub_44D36C(const vec3d &v, int id, NC_STACK_skeleton *skeleton)
{
    UAskeleton::Data *sklt = skeleton->GetSkelet();

    int v7 = 0;

    const UAskeleton::Polygon &tr = sklt->polygons[id];
    float nX = fabs(tr.A);
    float nY = fabs(tr.B);
    float nZ = fabs(tr.C);

    float maxAx = (nX <= nY ? nY : nX );
    maxAx = (maxAx <= nZ ? nZ : maxAx);

    if ( maxAx == nX )
    {
        int prev = sklt->polygons[id].num_vertices - 1;

        for (int i = 0; i < sklt->polygons[id].num_vertices; i++)
        {
            const UAskeleton::Vertex &cur = sklt->POO[ sklt->polygons[id].v[i] ];
            const UAskeleton::Vertex &prv = sklt->POO[ sklt->polygons[id].v[prev] ];

            if ( ( (prv.z <= v.z && v.z < cur.z) ||
                    (cur.z <= v.z && v.z < prv.z) ) &&
                    prv.y + (cur.y - prv.y) * (v.z - prv.z) / (cur.z - prv.z) > v.y )
            {
                v7 = v7 == 0;
            }

            prev = i;
        }
    }
    else if ( maxAx == nY )
    {
        int prev = sklt->polygons[id].num_vertices - 1;

        for (int i = 0; i < sklt->polygons[id].num_vertices; i++)
        {
            const UAskeleton::Vertex &cur = sklt->POO[ sklt->polygons[id].v[i] ];
            const UAskeleton::Vertex &prv = sklt->POO[ sklt->polygons[id].v[prev] ];

            if ( ( (prv.z <= v.z && v.z < cur.z) ||
                    (cur.z <= v.z && v.z < prv.z) ) &&
                    prv.x + (cur.x - prv.x) * (v.z - prv.z) / (cur.z - prv.z) > v.x )
            {
                v7 = v7 == 0;
            }

            prev = i;
        }
    }
    else if ( maxAx == nZ )
    {
        int prev = sklt->polygons[id].num_vertices - 1;

        for (int i = 0; i < sklt->polygons[id].num_vertices; i++)
        {
            const UAskeleton::Vertex &cur = sklt->POO[ sklt->polygons[id].v[i] ];
            const UAskeleton::Vertex &prv = sklt->POO[ sklt->polygons[id].v[prev] ];

            if ( ( (prv.y <= v.y && v.y < cur.y) ||
                    (cur.y <= v.y && v.y < prv.y) ) &&
                    prv.x + (cur.x - prv.x) * (v.y - prv.y) / (cur.y - prv.y) > v.x )
            {
                v7 = v7 == 0;
            }

            prev = i;
        }
    }
    return v7;
}

void NC_STACK_ypaworld::sub_44D8B8(ypaworld_arg136 *arg, const TSectorCollision &loc)
{
    UAskeleton::Data *skel = loc.sklt->GetSkelet();
    for ( size_t i = 0; i < skel->polygons.size(); i++)
    {
        UAskeleton::Polygon &tr = skel->polygons[i];
        vec3d norm = tr.Normal();

        float v11 = norm.dot(arg->vect);
        if ( v11 > 0.0 )
        {
            float v19 = -(norm.dot( arg->stPos ) + tr.D) / v11;
            if ( v19 > 0.0 && v19 <= 1.0 && v19 < arg->tVal )
            {
                vec3d px = arg->vect * v19 + arg->stPos;

                if ( sub_44D36C(px, i, loc.sklt) )
                {
                    arg->isect = 1;
                    arg->tVal = v19;
                    arg->isectPos = loc.pos + px;
                    arg->polyID = i;
                    arg->skel = loc.sklt->GetSkelet();
                }
            }
        }
    }
}


void NC_STACK_ypaworld::FFeedback_VehicleChanged()
{
    if ( _shellConfIsParsed )
    {
        if ( _preferences & (World::PREF_JOYDISABLE | World::PREF_FFDISABLE) )
            return;
    }

    if ( _userUnit )
    {
        _ffTimeStamp = _timeStamp;

        Input::Engine.ForceFeedback(Input::FF_STATE_STOP, Input::FF_TYPE_ALL);

        int effectType;
        float v13;
        float v14;
        float v15;
        float v16;
        float v17;
        float v18;
        float v19;
        float v22;
        float v24;
        float v25;


        switch ( _userUnit->_bact_type )
        {
        case BACT_TYPES_BACT:
            effectType = Input::FF_TYPE_HELIENGINE;
            v16 = 300.0;
            v13 = 800.0;
            v15 = 1.0;
            v14 = 2.0;
            v25 = 1.0;
            v18 = 0.0;
            v17 = 0.7;
            v24 = 0.3;
            v22 = 1.0;
            v19 = 0.5;
            break;

        case BACT_TYPES_TANK:
        case BACT_TYPES_CAR:
            effectType = Input::FF_TYPE_TANKENGINE;
            v16 = 200.0;
            v13 = 500.0;
            v15 = 0.6;
            v14 = 1.0;
            v25 = 1.0;
            v22 = 1.0;
            v17 = 0.1;
            v24 = 0.3;
            v19 = 0.4;
            v18 = 0.0;
            break;

        case BACT_TYPES_FLYER:
            effectType = Input::FF_TYPE_JETENGINE;
            v16 = 200.0;
            v13 = 500.0;
            v15 = 1.0;
            v14 = 2.0;
            v25 = 1.0;
            v18 = 0.0;
            v17 = 0.1;
            v24 = 0.3;
            v22 = 1.0;
            v19 = 0.75;
            break;

        default:
            effectType = -1;
            break;
        }

        if ( effectType != -1 )
        {
            float v4 = (_userUnit->_mass - v16) / (v13 - v16);
            float v5 = (_userUnit->_maxrot - v15) / (v14 - v15);

            float v21 = (v19 - v24) * v4 + v24;
            float v23 = (v18 - v22) * v4 + v22;

            float v20 = (v17 - v25) * v5 + v25;

            if ( v21 < v24)
                v21 = v24;
            else if (v21 > v19)
                v21 = v19;

            if ( v23 < v18 )
                v23 = v18;
            else if ( v23 > v22)
                v23 = v22;

            if ( v20 < v17)
                v20 = v17;
            else if ( v20 > v25)
                v20 = v25;

            _ffEffectType = effectType;
            _ffPeriod = v23;
            _ffMagnitude = v21;

            Input::Engine.ForceFeedback(Input::FF_STATE_START, effectType, v21, v23);
            Input::Engine.ForceFeedback(Input::FF_STATE_START, Input::FF_TYPE_ROTDAMPER, v20);
        }
    }
}




NC_STACK_ypabact *NC_STACK_ypaworld::yw_createUnit( int model_id)
{
    std::array<const std::string, 11> unit_classes_names
    {
        "dummy.class",      // 0
        "ypabact.class",    // 1
        "ypatank.class",    // 2
        "yparobo.class",    // 3
        "ypamissile.class", // 4
        "ypazepp.class",    // 5
        "ypaflyer.class",   // 6
        "ypaufo.class",     // 7
        "ypacar.class",     // 8
        "ypagun.class",     // 9
        "ypahovercraft.class" //10
    };

    NC_STACK_ypabact *bacto = NULL;

    // Find dead units
    for ( NC_STACK_ypabact * &unit : _deadCacheList )
    {
        if (unit->_bact_type == model_id)
        {
            bacto = unit;
            break;
        }
    }

    if ( !bacto )
    {
        bacto = Nucleus::CTFInit<NC_STACK_ypabact>(unit_classes_names[model_id],
            {{NC_STACK_ypabact::BACT_ATT_WORLD, this}} );

        if ( !bacto )
            return NULL;
    }

    bacto->Renew(); // Reset bact

    bacto->_gid = bact_id;
    bacto->_owner = 0;

    bacto->_rotation = mat3x3::Ident();

    bact_id++;

    return bacto;
}




void NC_STACK_ypaworld::RenderAdditionalBeeBox(Common::Point sect, TRenderingSector *sct, baseRender_msg *bs77)
{
    sct->dword8 = 0;
    sct->dword4 = 0;

    if ( IsSector( sect ) )
    {
        sct->dword4 = 1;
        sct->p_cell = &_cells( sect );

        vec3d pos = World::SectorIDToCenterPos3( sect );
        pos.y = sct->p_cell->height;

        _beeBox->SetPosition(pos);

        if ( _beeBox->Render(bs77, NULL) )
        {
            sct->dword8 = 1;
        }
    }
}

void NC_STACK_ypaworld::RenderSector(TRenderingSector *sct, baseRender_msg *bs77)
{
    if ( sct->dword8 )
    {
        cellArea *pcell = sct->p_cell;

        int v22 = 0;

        vec3d scel;
        if ( pcell->PurposeType == cellArea::PT_CONSTRUCTING )
        {
            auto it = _inBuildProcess.find(pcell->Id);
            if (it != _inBuildProcess.end())
            {
                TConstructInfo &bldProc = it->second;

                scel = vec3d::OY((float)bldProc.Time / (float)bldProc.EndTime);

                pcell->type_id = _buildProtos[ bldProc.BuildID ].SecType;
                pcell->SectorType = _secTypeArray[ pcell->type_id ].SectorType;

                v22 = 1;
            }
        }

        int v17, v20;

        if ( pcell->SectorType == 1 )
        {
            v17 = 0;
            v20 = 1;
        }
        else
        {
            v17 = -1;
            v20 = 3;
        }

        for (int zz = 0; zz < v20; zz++)
        {
            for (int xx = 0; xx < v20; xx++)
            {
                vec3d pos = sct->p_cell->CenterPos + vec3d((v17 + xx) * 300.0, 0.0, (v17 + zz) * 300.0);

                if ( v22 )
                {
                    NC_STACK_base *bld = _legoArray[ _secTypeArray[ pcell->type_id ].SubSectors.At(xx, zz)->HPModels[0] ].Base;

                    bld->SetStatic(false);

                    bld->SetScale(scel, NC_STACK_base::UF_Y); //Scale only Y
                    bld->SetPosition(pos);

                    NC_STACK_base::CheckOpts( &pcell->BldVPOpts.At(xx, zz), bld );

                    bld->Render(bs77, pcell->BldVPOpts.At(xx, zz));

                    bld->SetStatic(true);
                }
                else
                {
                    NC_STACK_base *bld = _legoArray[ GetLegoBld(pcell, xx, zz) ].Base;

                    bld->SetPosition(pos);

                    NC_STACK_base::CheckOpts( &pcell->BldVPOpts.At(xx, zz), bld );

                    bld->Render(bs77, pcell->BldVPOpts.At(xx, zz));
                }
            }
        }
    }

    for ( NC_STACK_ypabact* &bact : sct->p_cell->unitsList )
    {
        if ( sct->dword8 || bact->_bact_type == BACT_TYPES_ROBO)
            bact->Render(bs77);
    }
}

void NC_STACK_ypaworld::yw_renderSky(baseRender_msg *rndr_params)
{
    if ( _skyObject )
    {
        float v6 = rndr_params->maxZ;
        uint32_t flags = rndr_params->flags;

        _skyObject->SetPosition( _viewerBact->_position + vec3d::OY(_skyHeight) );

        rndr_params->maxZ = 32000.0;
        rndr_params->flags = GFX::RFLAGS_SKY | GFX::RFLAGS_COMPUTED_COLOR;

        _skyObject->Render(rndr_params, NULL);

        rndr_params->maxZ = v6;
        rndr_params->flags = flags;
    }
}


bool NC_STACK_ypaworld::IsVisibleMapPos(vec2d pos)
{
    Common::Point pt( ((pos.x + 150) / 300) / 4, ((-pos.y + 150) / 300) / 4 );

    if ( !IsGamePlaySector( pt ) || !_viewerBact )
        return false;

    Common::Point dist = _viewerBact->_cellId.AbsDistance( pt );
    if ( dist.x + dist.y <= (_renderSectors - 1) / 2 )
        return true;

    return false;
}

void NC_STACK_ypaworld::RenderSuperWave(vec2d pos, vec2d fromPos, baseRender_msg *arg)
{
    if ( !_stoudsonWaveVehicleId )
        return;

    if ( pos.x > 0.0 && pos.y < 0.0 && pos.x < _mapLength.x && -_mapLength.y < pos.y )
    {
        if ( IsVisibleMapPos(pos) )
        {
            int v10 = _vhclProtos[_stoudsonWaveVehicleId].vp_normal;

            NC_STACK_base *wall_base = _vhclModels.at(v10);

            if ( wall_base )
            {
                float v28 = 0.0;

                int v23 = (pos.x + 150) / 300;
                int v26 = (-pos.y + 150) / 300;

                if ( (v23 & 3) && (v26 & 3) )
                {
                    v28 = _cells((v23 / 4), (v26 / 4)).height;
                }
                else
                {
                    ypaworld_arg136 v22;
                    v22.vect = vec3d::OY(50000.0);
                    v22.stPos.x = pos.x;
                    v22.stPos.y = -25000.0;
                    v22.stPos.z = pos.y;
                    v22.flags = 0;

                    ypaworld_func136(&v22);

                    if ( v22.isect )
                    {
                        v28 = v22.isectPos.y;
                    }
                }


                wall_base->TForm().Pos = vec3d(pos.x, v28, pos.y);

                vec2d delt = pos - fromPos;
                delt.normalise();

                wall_base->TForm().SclRot =  mat3x3(delt.y,   0, -delt.x,
                                                      0, 1.0,    0,
                                                    delt.x, 0.0,  delt.y);

                wall_base->Render(arg, NULL);
            }
        }
    }
}

void NC_STACK_ypaworld::RenderSuperItems(baseRender_msg *arg)
{
    // Render super items
    for ( const TMapSuperItem &sitem : _levelInfo.SuperItems )
    {
        if ( sitem.State == TMapSuperItem::STATE_TRIGGED )
        {
            vec2d pos = World::SectorIDToCenterPos2( sitem.CellId );

            float v14 = sqrt( POW2(_mapLength.x) + POW2(_mapLength.y) );

            if ( sitem.CurrentRadius > 300 && sitem.CurrentRadius < v14 )
            {
                float v17 = (2 * sitem.CurrentRadius) * C_PI / 300.0;

                if ( v17 > 2.0 )
                {
                    float v9 = 6.283 / v17;

                    for (float j = 0.0; j < 6.283; j = j + v9 )
                    {
                        vec2d wallpos = vec2d(cos(j), sin(j)) * sitem.CurrentRadius + pos;
                        RenderSuperWave(wallpos, pos, arg);
                    }
                }
            }
        }
    }
}

void NC_STACK_ypaworld::PrepareFiller(cellArea *sct, cellArea *sct2, float v9h, float v8h, bool vertical, TCellFillerCh *out, bool force)
{
    int x = _secTypeArray[ sct->type_id ].SurfaceType;
    int y = _secTypeArray[ sct2->type_id ].SurfaceType;

    if (!force && (out->Id1 == x && out->Id2 == y &&
        out->Heights[0] == sct->height && out->Heights[1] == sct2->height &&
        out->Heights[2] == v8h && out->Heights[3] == v9h))
        return;

    NC_STACK_base *bs;
    if (vertical)
        bs = _fillersVertical(x, y);
    else
        bs = _fillersHorizontal(x, y);

    UAskeleton::Data *skel = bs->GetSkeleton()->GetSkelet();

    vec2d pos = World::SectorIDToCenterPos2( sct2->CellId );

    bs->SetPosition( vec3d::X0Z( pos ), NC_STACK_base::UF_XZ );

    for (int i = 0; i < 4; i++)
        skel->POO[i].y = sct->height;

    for (int i = 4; i < 8; i++)
        skel->POO[i].y = sct2->height;

    skel->POO[8].y = v8h;
    skel->POO[9].y = v9h;

    bs->RecalcInternal(true);
    bs->MakeCoordsCache();

    out->FreeVBO();

    bs->MakeCache(out);

    out->MakeVBO();

    out->Id1 = x;
    out->Id2 = y;
    out->Heights[0] = sct->height;
    out->Heights[1] = sct2->height;
    out->Heights[2] = v8h;
    out->Heights[3] = v9h;
}

void NC_STACK_ypaworld::PrepareAllFillers()
{
    for (int i = 0; i < _mapSize.x - 1; i++)
    {
        for (int j = 0; j < _mapSize.y - 2; j++)
        {
            cellArea *sct = &_cells(i, j);
            cellArea *sct2 = &_cells(i, j + 1);

            float h;
            if (i == _mapSize.x - 1)
                h = sct2->averg_height;
            else
                h = _cells(i + 1, j + 1).averg_height;

            PrepareFiller(sct, sct2, sct2->averg_height, h, false, &_cellsHFCache(i, j), true);
        }
    }

    for (int i = 0; i < _mapSize.x - 2; i++)
    {
        for (int j = 0; j < _mapSize.y - 1; j++)
        {
            cellArea *sct = &_cells(i, j);
            cellArea *sct2 = &_cells(i + 1, j);

            float h;
            if (i == _mapSize.x - 1)
                h = sct2->averg_height;
            else
                h = _cells(i + 1, j + 1).averg_height;

            PrepareFiller(sct, sct2, sct2->averg_height, h, true, &_cellsVFCache(i, j), true);
        }
    }
}




TRenderingSector rendering_sectors[YW_RENDER_SECTORS_DEF * 2][ YW_RENDER_SECTORS_DEF * 2];

void NC_STACK_ypaworld::RenderFillers(baseRender_msg *arg)
{
    //Render landscape linking parts
    for (int i = 0; i < _renderSectors; i++)
    {
        for (int j = 0; j < _renderSectors - 1; j++)
        {
            TRenderingSector &sct = rendering_sectors[j][i];
            TRenderingSector &sct2 = rendering_sectors[j + 1][i];

            if (sct.dword4 == 1 && sct2.dword4 == 1 && (sct.dword8 == 1 || sct2.dword8 == 1))
            {
                float h;
                if (rendering_sectors[j + 1][i + 1].dword4 == 1)
                    h = rendering_sectors[j + 1][i + 1].p_cell->averg_height;
                else
                    h = sct2.p_cell->averg_height;

                TCellFillerCh &filler = _cellsVFCache( sct2.p_cell->CellId.x - 1, sct2.p_cell->CellId.y );
                PrepareFiller(sct.p_cell, sct2.p_cell, sct2.p_cell->averg_height, h, true, &filler);
                filler.Render(arg);
            }
        }
    }

    for (int i = 0; i < _renderSectors - 1; i++)
    {
        for (int j = 0; j < _renderSectors; j++)
        {
            TRenderingSector &sct = rendering_sectors[j][i];
            TRenderingSector &sct2 = rendering_sectors[j][i + 1];

            if (sct.dword4 == 1 && sct2.dword4 == 1 && (sct.dword8 == 1 || sct2.dword8 == 1))
            {
                float h;
                if (rendering_sectors[j + 1][i + 1].dword4 == 1)
                    h = rendering_sectors[j + 1][i + 1].p_cell->averg_height;
                else
                    h = sct2.p_cell->averg_height;

                TCellFillerCh &filler = _cellsHFCache( sct2.p_cell->CellId.x, sct2.p_cell->CellId.y - 1 );
                PrepareFiller(sct.p_cell, sct2.p_cell, sct2.p_cell->averg_height, h, false, &filler);
                filler.Render(arg);
            }
        }
    }
}

static void yw_ApplyTransientVPTrailOnly(NC_STACK_ypaworld::TTransientVP &fx, bool trailOnly)
{
    if ( trailOnly && fx.vp )
        fx.vp->skipGeometry = true;
}

int32_t NC_STACK_ypaworld::SpawnTransientVP(int32_t modelId, const vec3d &pos, const mat3x3 &rot, int32_t lifeTime, float scale, const World::TVisualTint &tint, const vec3d &axisScale, const vec3d &spin, bool trailOnly)
{
    if ( modelId <= 0 || modelId >= (int32_t)_vhclModels.size() || lifeTime < 0 )
        return 0;

    NC_STACK_base *base = _vhclModels.at(modelId);

    if ( base )
    {
        _transientVPs.emplace_back(base, pos, rot, lifeTime);
        TTransientVP &fx = _transientVPs.back();
        fx.id = _nextTransientVPId++;
        fx.scale = scale > 0.0f ? scale : 1.0f;
        fx.axisScale = vec3d(axisScale.x > 0.0f ? axisScale.x : 1.0f,
                             axisScale.y > 0.0f ? axisScale.y : 1.0f,
                             axisScale.z > 0.0f ? axisScale.z : 1.0f);
        fx.spin = spin;
        fx.tint = tint;
        yw_ApplyTransientVPTrailOnly(fx, trailOnly);
        return fx.id;
    }

    return 0;
}

void NC_STACK_ypaworld::SpawnChainFX(const World::TChainFXConfig &config, const vec3d &pos, const mat3x3 &rot)
{
    if ( config.duration <= 0 || config.vp_models.empty() )
        return;

    std::vector<NC_STACK_base *> bases;
    bases.reserve(config.vp_models.size());
    std::vector<World::TVisualTint> tints;
    tints.reserve(config.vp_models.size());

    for (const World::TChainFXVPModel &vpModel : config.vp_models)
    {
        int16_t modelId = vpModel.model;
        if ( modelId <= 0 || modelId >= (int32_t)_vhclModels.size() )
            continue;

        NC_STACK_base *base = _vhclModels.at(modelId);
        if ( base )
        {
            bases.push_back(base);
            tints.push_back(vpModel.has_tint ? vpModel.tint : World::TVisualTint());
        }
    }

    if ( bases.empty() )
        return;

    vec3d spawnPos = pos + rot.Transform(config.offset);
    _transientVPs.emplace_back(bases.front(), spawnPos, rot, config.duration);

    TTransientVP &fx = _transientVPs.back();
    fx.chainFX = true;
    fx.chainBases = std::move(bases);
    fx.chainTints = std::move(tints);
    fx.chainIndex = 0;
    if ( !fx.chainTints.empty() )
        fx.tint = fx.chainTints.front();
    fx.startScale = config.start_size >= 0.0 ? config.start_size : 0.0;
    fx.endScale = config.end_size >= 0.0 ? config.end_size : 0.0;
}

static int yw_RandomInRange(int minValue, int maxValue)
{
    if ( maxValue < minValue )
        std::swap(minValue, maxValue);

    if ( minValue == maxValue )
        return minValue;

    double randomPart = (double)rand() / ((double)RAND_MAX + 1.0);
    int64_t range = (int64_t)maxValue - minValue;
    return minValue + (int)((range + 1) * randomPart);
}

bool NC_STACK_ypaworld::UpdateRandomFXTimer(int intervalMin, int intervalMax, int32_t &nextTime)
{
    if ( intervalMin <= 0 || intervalMax <= 0 )
    {
        nextTime = 0;
        return false;
    }

    if ( intervalMax < intervalMin )
        std::swap(intervalMin, intervalMax);

    if ( nextTime <= 0 )
    {
        nextTime = _timeStamp + yw_RandomInRange(intervalMin, intervalMax);
        return false;
    }

    if ( _timeStamp < nextTime )
        return false;

    nextTime = _timeStamp + yw_RandomInRange(intervalMin, intervalMax);
    return true;
}

int32_t NC_STACK_ypaworld::SpawnRandomizedTransientVP(int32_t modelId, const vec3d &ownerPos, float randomPos, const World::TVisualTint &tint, int32_t lifeTime, float scale, const vec3d &offset, const vec3d &axisScale, const vec3d &spin, bool trailOnly)
{
    if ( modelId <= 0 )
        return 0;

    vec3d pos = ownerPos + offset;
    if ( randomPos > 0.0 )
    {
        pos.x += (((float)rand() / (float)RAND_MAX) * 2.0 - 1.0) * randomPos;
        pos.y += (((float)rand() / (float)RAND_MAX) * 2.0 - 1.0) * randomPos;
        pos.z += (((float)rand() / (float)RAND_MAX) * 2.0 - 1.0) * randomPos;
    }

    return SpawnTransientVP(modelId, pos, mat3x3::Ident(), lifeTime, scale, tint, axisScale, spin, trailOnly);
}

void NC_STACK_ypaworld::UpdateDecorationFX(const World::TDecorationFXConfig &config, int32_t &nextTime, const vec3d &ownerPos, int32_t *persistentId)
{
    const World::TVisualTint tint = config.vp_tint;

    if ( config.mode == World::DECORATION_FX_PERSISTENT )
    {
        nextTime = 0;

        if ( !persistentId || config.vp <= 0 )
            return;

        if ( !HasTransientVP(*persistentId) )
            *persistentId = SpawnTransientVP(config.vp, ownerPos + config.offset, mat3x3::Ident(), 0, 1.0, tint, config.vp_scale, config.vp_spin, config.trail_only);

        return;
    }

    if ( persistentId && *persistentId > 0 )
    {
        RemoveTransientVP(*persistentId);
        *persistentId = 0;
    }

    int countMin = std::max(0, std::min(config.count_min, 32));
    int countMax = std::max(0, std::min(config.count_max, 32));

    if ( config.vp <= 0 || countMin <= 0 || countMax <= 0 )
    {
        nextTime = 0;
        return;
    }

    if ( countMax < countMin )
        std::swap(countMin, countMax);

    if ( !UpdateRandomFXTimer(config.interval_min, config.interval_max, nextTime) )
        return;

    int spawnCount = yw_RandomInRange(countMin, countMax);
    for (int i = 0; i < spawnCount; i++)
        SpawnRandomizedTransientVP(config.vp, ownerPos, config.random_pos,
                                   tint,
                                   config.duration > 0 ? config.duration : 1000,
                                   1.0,
                                   config.offset,
                                   config.vp_scale,
                                   config.vp_spin,
                                   config.trail_only);
}

int32_t NC_STACK_ypaworld::SpawnAttachedTransientVP(int32_t modelId, NC_STACK_ypabact *owner, const vec3d &localOffset, int32_t lifeTime, float scale, bool useOwnerTransform, const World::TVisualTint &tint, const vec3d &axisScale, const vec3d &spin, bool playerFirstPersonOnly, const vec3d &localRotation, bool hideInOwnerMissileCamera, bool trailOnly)
{
    if ( !owner || modelId <= 0 || modelId >= (int32_t)_vhclModels.size() )
        return 0;

    if ( lifeTime < 0 )
        return 0;

    NC_STACK_base *base = _vhclModels.at(modelId);

    if ( !base )
        return 0;

    _transientVPs.emplace_back(base, owner->_position, owner->_rotation, lifeTime);

    TTransientVP &fx = _transientVPs.back();
    fx.id = _nextTransientVPId++;
    fx.followOwner = true;
    fx.followOwnerGid = owner->_gid;
    fx.followLocalOffset = localOffset;
    fx.followUseOwnerTransform = useOwnerTransform;
    fx.playerFirstPersonOnly = playerFirstPersonOnly;
    fx.localRotation = localRotation;
    fx.hideInOwnerMissileCamera = hideInOwnerMissileCamera;
    fx.scale = scale > 0.0 ? scale : 1.0;
    fx.axisScale = vec3d(axisScale.x > 0.0f ? axisScale.x : 1.0f,
                         axisScale.y > 0.0f ? axisScale.y : 1.0f,
                         axisScale.z > 0.0f ? axisScale.z : 1.0f);
    fx.spin = spin;
    fx.tint = tint;
    yw_ApplyTransientVPTrailOnly(fx, trailOnly);
    return fx.id;
}

int32_t NC_STACK_ypaworld::SpawnAttachedStatusTransientVP(int32_t modelId, NC_STACK_ypabact *owner,
                                                          const vec3d &localOffset, int32_t lifeTime,
                                                          bool trailOnly, bool rotateOffsetWithOwner,
                                                          const vec3d &axisScale)
{
    int32_t id = SpawnAttachedTransientVP(modelId, owner, localOffset, lifeTime,
                                          1.0, false, World::TVisualTint(), axisScale,
                                          vec3d(0.0, 0.0, 0.0), false,
                                          vec3d(0.0, 0.0, 0.0), false, trailOnly);
    if ( id <= 0 || _transientVPs.empty() )
        return id;

    TTransientVP &fx = _transientVPs.back();
    if ( fx.id != id )
        return id;

    fx.followRotateOffset = rotateOffsetWithOwner;

    return id;
}

bool NC_STACK_ypaworld::HasTransientVP(int32_t id) const
{
    if ( id <= 0 )
        return false;

    for (const TTransientVP &fx : _transientVPs)
    {
        if ( fx.id == id )
            return true;
    }

    return false;
}

void NC_STACK_ypaworld::RemoveTransientVP(int32_t id)
{
    if ( id <= 0 )
        return;

    for (auto it = _transientVPs.begin(); it != _transientVPs.end(); ++it)
    {
        if ( it->id == id )
        {
            _transientVPs.erase(it);
            return;
        }
    }
}

static NC_STACK_ypabact *yw_FindLiveBactByGidInList(World::RefBactList &list, int32_t gid);

static NC_STACK_ypabact *yw_FindLiveMissileByGidInList(World::MissileList &list, int32_t gid)
{
    for (NC_STACK_ypamissile *missile : list)
    {
        if ( missile->_gid == gid )
        {
            if ( missile->_kidRef.IsListType(World::BLIST_CACHE) || missile->_status == BACT_STATUS_DEAD )
                return NULL;

            return missile;
        }

        NC_STACK_ypabact *kid = yw_FindLiveBactByGidInList(missile->_kidList, gid);
        if ( kid )
            return kid;

        NC_STACK_ypabact *childMissile = yw_FindLiveMissileByGidInList(missile->_missiles_list, gid);
        if ( childMissile )
            return childMissile;
    }

    return NULL;
}

static NC_STACK_ypabact *yw_FindLiveBactByGidInList(World::RefBactList &list, int32_t gid)
{
    for (NC_STACK_ypabact *unit : list)
    {
        if ( unit->_gid == gid )
        {
            if ( unit->_kidRef.IsListType(World::BLIST_CACHE) || unit->_status == BACT_STATUS_DEAD )
                return NULL;

            return unit;
        }

        NC_STACK_ypabact *kid = yw_FindLiveBactByGidInList(unit->_kidList, gid);
        if ( kid )
            return kid;

        NC_STACK_ypabact *missile = yw_FindLiveMissileByGidInList(unit->_missiles_list, gid);
        if ( missile )
            return missile;
    }

    return NULL;
}

static bool yw_IsAliveBuildingSpawnedUnit(NC_STACK_ypabact *unit)
{
    if ( !unit )
        return false;

    if ( unit->_kidRef.IsListType(World::BLIST_CACHE) )
        return false;

    if ( unit->_status == BACT_STATUS_DEAD )
        return false;

    if ( unit->_status_flg & (BACT_STFLAG_DEATH1 | BACT_STFLAG_DEATH2) )
        return false;

    return true;
}

static int yw_CountBuildingSpawnedUnits(NC_STACK_ypaworld *world, cellArea &cell)
{
    int aliveCount = 0;

    for (auto it = cell.BuildingSpawnedGids.begin(); it != cell.BuildingSpawnedGids.end();)
    {
        NC_STACK_ypabact *unit = yw_FindLiveBactByGidInList(world->_unitsList, *it);

        if ( yw_IsAliveBuildingSpawnedUnit(unit) )
        {
            aliveCount++;
            ++it;
        }
        else
        {
            it = cell.BuildingSpawnedGids.erase(it);
        }
    }

    return aliveCount;
}

static bool yw_IsBuildingSpawnEnemy(uint8_t owner, NC_STACK_ypabact *unit)
{
    if ( !yw_IsAliveBuildingSpawnedUnit(unit) )
        return false;

    if ( unit->_bact_type == BACT_TYPES_MISSLE )
        return false;

    if ( unit->_status == BACT_STATUS_CREATE || unit->_status == BACT_STATUS_BEAM )
        return false;

    if ( unit->_owner == World::OWNER_0 || unit->_owner == owner )
        return false;

    return true;
}

static bool yw_IsActiveBuildingSpawnerCell(cellArea &cell)
{
    if ( cell.PurposeType != cellArea::PT_BUILDINGS &&
         cell.PurposeType != cellArea::PT_POWERSTATION )
        return false;

    return cell.GetEnergy() > 0;
}

static bool yw_BuildingHasEnemyNearbyInUnitTree(uint8_t owner, const vec3d &centerPos, float radiusSq, NC_STACK_ypabact *unit)
{
    if ( yw_IsBuildingSpawnEnemy(owner, unit) &&
         (unit->_position.XZ() - centerPos.XZ()).square() <= radiusSq )
        return true;

    if ( unit )
    {
        for (NC_STACK_ypabact *kid : unit->_kidList)
        {
            if ( yw_BuildingHasEnemyNearbyInUnitTree(owner, centerPos, radiusSq, kid) )
                return true;
        }
    }

    return false;
}

static bool yw_BuildingHasEnemyNearby(NC_STACK_ypaworld *world, cellArea &cell, const World::TBuildingProto &proto)
{
    float radius = proto.spawn_trigger_radius;
    float radiusSq = radius * radius;

    for (NC_STACK_ypabact *unit : world->_unitsList)
        if ( yw_BuildingHasEnemyNearbyInUnitTree(cell.owner, cell.CenterPos, radiusSq, unit) )
            return true;

    return false;
}

static bool yw_IsBuildingSpawnCandidateCell(cellArea &buildingCell, cellArea *spawnCell, const char **failReason)
{
    if ( !spawnCell )
    {
        if ( failReason )
            *failReason = "no sector";
        return false;
    }

    if ( spawnCell == &buildingCell )
    {
        if ( failReason )
            *failReason = "building center sector";
        return false;
    }

    if ( !spawnCell->IsGamePlaySector() )
    {
        if ( failReason )
            *failReason = "border sector";
        return false;
    }

    if ( spawnCell->PurposeType != cellArea::PT_NONE )
    {
        if ( failReason )
            *failReason = "occupied purpose sector";
        return false;
    }

    return true;
}

static bool yw_AdjustBuildingSpawnHeight(NC_STACK_ypaworld *world, const World::TBuildingProto &proto, vec3d *pos, const char **failReason)
{
    const std::vector<World::TVhclProto> &protos = world->GetVhclProtos();
    if ( proto.spawn_vehicle <= 0 || (size_t)proto.spawn_vehicle >= protos.size() )
    {
        if ( failReason )
            *failReason = "bad vehicle id";
        return false;
    }

    ypaworld_arg136 ground;
    ground.stPos = pos->X0Z() - vec3d::OY(30000.0);
    ground.vect = vec3d::OY(50000.0);
    ground.flags = 0;

    world->ypaworld_func136(&ground);
    if ( !ground.isect )
    {
        if ( failReason )
            *failReason = "no ground hit";
        return false;
    }

    // Building spawners are intended for airborne defensive units.
    // Spawn them above the building cell, not at ground level.
    // UA uses negative Y for higher altitude, so subtracting extraHeight
    // places the unit above the terrain/building.
    const float baseAirSpawnHeight = 650.0;
    const float randomAirSpawnHeight = 250.0;
    float extraHeight = baseAirSpawnHeight;

    extraHeight += ((float)rand() / (float)RAND_MAX) * randomAirSpawnHeight;

    pos->y = ground.isectPos.y - protos[proto.spawn_vehicle].overeof - extraHeight;
    return true;
}

static bool yw_IsBuildingSpawnPositionClear(NC_STACK_ypaworld *world, cellArea &buildingCell, const World::TBuildingProto &proto, vec3d *pos, const char **failReason)
{
    yw_130arg sect;
    sect.pos_x = pos->x;
    sect.pos_z = pos->z;

    if ( !world->GetSectorInfo(&sect) )
    {
        if ( failReason )
            *failReason = "outside map";
        return false;
    }

    // Building spawners intentionally spawn inside their own building cell,
    // above the powerstation/building. Adjacent cells are still validated
    // by the old candidate rules if future code ever uses them again.
    if ( sect.pcell != &buildingCell && !yw_IsBuildingSpawnCandidateCell(buildingCell, sect.pcell, failReason) )
        return false;

    float localX = pos->x - sect.pcell->CenterPos.x;
    float localZ = pos->z - sect.pcell->CenterPos.z;
    float edgeLimit = World::CVSectorHalfLength - 180.0;
    if ( fabs(localX) > edgeLimit || fabs(localZ) > edgeLimit )
    {
        if ( failReason )
            *failReason = "too close to sector edge";
        return false;
    }

    if ( !yw_AdjustBuildingSpawnHeight(world, proto, pos, failReason) )
        return false;

    float spawnRadius = 20.0;
    const std::vector<World::TVhclProto> &protos = world->GetVhclProtos();

    if ( proto.spawn_vehicle > 0 && (size_t)proto.spawn_vehicle < protos.size() && protos[proto.spawn_vehicle].radius > 0.0 )
        spawnRadius = protos[proto.spawn_vehicle].radius;

    for (NC_STACK_ypabact *unit : sect.pcell->unitsList)
    {
        if ( !yw_IsAliveBuildingSpawnedUnit(unit) || unit->_bact_type == BACT_TYPES_MISSLE )
            continue;

        float otherRadius = unit->_radius > 0.0 ? unit->_radius : 20.0;
        float minDist = spawnRadius + otherRadius + 20.0;

        if ( (unit->_position.XZ() - pos->XZ()).square() < minDist * minDist )
        {
            if ( failReason )
                *failReason = "unit collision";
            return false;
        }
    }

    return true;
}

static void yw_LogBuildingSpawnPositionFailure(NC_STACK_ypaworld *world, cellArea &cell, const World::TBuildingProto &proto, const char *reason)
{
    static int32_t lastLogTime = 0;

    if ( !world )
        return;

    if ( lastLogTime && world->_timeStamp - lastLogTime < 3000 )
        return;

    lastLogTime = world->_timeStamp;
    ypa_log_out("Building spawner: no safe spawn position building=%d vehicle=%d cell=%d,%d reason=%s\n",
                proto.Index,
                proto.spawn_vehicle,
                cell.CellId.x,
                cell.CellId.y,
                reason ? reason : "unknown");
}

static NC_STACK_yparobo *yw_FindBuildingSpawnOwnerRobo(NC_STACK_ypaworld *world, uint8_t owner)
{
    if ( !world || owner == World::OWNER_0 )
        return NULL;

    for (NC_STACK_ypabact *unit : world->_unitsList)
    {
        if ( unit->_bact_type != BACT_TYPES_ROBO ||
             unit->_owner != owner ||
             unit->_status == BACT_STATUS_DEAD ||
             unit->_kidRef.IsListType(World::BLIST_CACHE) ||
             (unit->_status_flg & (BACT_STFLAG_DEATH1 | BACT_STFLAG_DEATH2)) )
            continue;

        return dynamic_cast<NC_STACK_yparobo *>(unit);
    }

    return NULL;
}

static void yw_LogBuildingSpawnOwnerFailure(NC_STACK_ypaworld *world, cellArea &cell, const World::TBuildingProto &proto)
{
    static int32_t lastLogTime = 0;

    if ( !world )
        return;

    if ( lastLogTime && world->_timeStamp - lastLogTime < 3000 )
        return;

    lastLogTime = world->_timeStamp;
    ypa_log_out("Building spawner: no live owner robo building=%d owner=%d vehicle=%d cell=%d,%d\n",
                proto.Index,
                cell.owner,
                proto.spawn_vehicle,
                cell.CellId.x,
                cell.CellId.y);
}

static bool yw_FindBuildingSpawnPosition(NC_STACK_ypaworld *world, cellArea &cell, const World::TBuildingProto &proto, vec3d *outPos)
{
    const char *lastFailReason = "no candidate";

    // Building spawners are now intentionally local: spawned units appear
    // above the same powerstation/building cell, with limited random X/Z
    // jitter. This makes Sulg infected powerstations look like they are
    // emitting airborne spores from themselves instead of from nearby sectors.
    const float randomXZ = World::CVSectorHalfLength - 260.0;
    const int maxAttempts = 16;

    for (int attempt = 0; attempt < maxAttempts; attempt++)
    {
        vec3d pos = cell.CenterPos;

        float rndX = ((float)rand() / (float)RAND_MAX) * 2.0 - 1.0;
        float rndZ = ((float)rand() / (float)RAND_MAX) * 2.0 - 1.0;

        pos.x += rndX * randomXZ + 37.0;
        pos.z += rndZ * randomXZ - 41.0;

        if ( yw_IsBuildingSpawnPositionClear(world, cell, proto, &pos, &lastFailReason) )
        {
            *outPos = pos;
            return true;
        }
    }

    yw_LogBuildingSpawnPositionFailure(world, cell, proto, lastFailReason);
    return false;
}

static NC_STACK_ypabact *yw_CreateBuildingSpawnedUnit(NC_STACK_ypaworld *world, cellArea &cell, const World::TBuildingProto &proto, const vec3d &pos)
{
    NC_STACK_yparobo *ownerRobo = yw_FindBuildingSpawnOwnerRobo(world, cell.owner);
    if ( !ownerRobo )
    {
        yw_LogBuildingSpawnOwnerFailure(world, cell, proto);
        return NULL;
    }

    ypaworld_arg146 arg146;
    arg146.vehicle_id = proto.spawn_vehicle;
    arg146.pos = pos;

    NC_STACK_ypabact *unit = world->ypaworld_func146(&arg146);
    if ( !unit )
        return NULL;

    unit->_owner = cell.owner;
    unit->_host_station = ownerRobo;
    unit->_carrier_spawn_root_gid = 0;
    unit->_carrier_spawn_root_vehicle = 0;

    if ( unit->_spawn_units )
        unit->_spawn_last_time = unit->_clock > 0 ? unit->_clock : 1;

    unit->setBACT_bactCollisions(ownerRobo->getBACT_bactCollisions());
    ownerRobo->AddSubject(unit);

    setTarget_msg target;
    target.tgt_type = BACT_TGT_TYPE_CELL;
    target.priority = 0;
    target.tgt_pos = pos;
    unit->SetTarget(&target);

    if ( !proto.spawn_instant )
    {
        setState_msg state;
        state.setFlags = 0;
        state.unsetFlags = 0;
        state.newStatus = BACT_STATUS_CREATE;
        unit->SetState(&state);

        unit->_scale_time = unit->_energy_max * 0.2;
    }

    world->HistoryAktCreate(unit);

    return unit;
}

static void yw_UpdateBuildingSpawner(NC_STACK_ypaworld *world, cellArea &cell, const World::TBuildingProto &proto)
{
    if ( !world || world->_isNetGame )
        return;

    if ( !proto.spawn_units )
        return;

    if ( cell.owner == World::OWNER_0 || cell.owner == World::OWNER_7 )
        return;

    if ( cell.BuildingSpawnInitialOwner == 0 )
        cell.BuildingSpawnInitialOwner = cell.owner;

    if ( cell.owner != cell.BuildingSpawnInitialOwner )
        return;

    if ( proto.spawn_vehicle <= 0 || (size_t)proto.spawn_vehicle >= world->GetVhclProtos().size() )
        return;

    if ( !yw_IsActiveBuildingSpawnerCell(cell) )
        return;

    if ( proto.spawn_trigger_radius <= 0.0 )
        return;

    int interval = proto.spawn_interval > 0 ? proto.spawn_interval : 5000;
    if ( interval < 1000 )
        interval = 1000;

    if ( cell.BuildingSpawnLastTime && world->_timeStamp - cell.BuildingSpawnLastTime < interval )
        return;

    int maxActive = proto.spawn_max_active > 0 ? proto.spawn_max_active : 1;
    int activeCount = yw_CountBuildingSpawnedUnits(world, cell);
    if ( activeCount >= maxActive )
        return;

    if ( !yw_BuildingHasEnemyNearby(world, cell, proto) )
        return;

    cell.BuildingSpawnLastTime = world->_timeStamp;

    int spawnCount = proto.spawn_count > 0 ? proto.spawn_count : 1;
    if ( spawnCount > 8 )
        spawnCount = 8;

    int remainingSlots = maxActive - activeCount;
    if ( spawnCount > remainingSlots )
        spawnCount = remainingSlots;

    for (int i = 0; i < spawnCount; i++)
    {
        vec3d spawnPos;
        if ( !yw_FindBuildingSpawnPosition(world, cell, proto, &spawnPos) )
            continue;

        NC_STACK_ypabact *unit = yw_CreateBuildingSpawnedUnit(world, cell, proto, spawnPos);
        if ( unit )
            cell.BuildingSpawnedGids.push_back(unit->_gid);
    }
}

static mat3x3 yw_BuildTransientVPRotationMatrix(const vec3d &degrees)
{
    const float degToRad = 0.01745329251994329577f;
    vec3d angle = degrees * degToRad;
    mat3x3 rot = mat3x3::Ident();

    if ( angle.x != 0.0 )
        rot *= mat3x3::RotateX(angle.x);
    if ( angle.y != 0.0 )
        rot *= mat3x3::RotateY(angle.y);
    if ( angle.z != 0.0 )
        rot *= mat3x3::RotateZ(angle.z);

    return rot;
}

static bool yw_HasTransientVPRotation(const vec3d &rotation)
{
    return rotation.x != 0.0 || rotation.y != 0.0 || rotation.z != 0.0;
}

static void yw_RenderTransientVPs(NC_STACK_ypaworld *world, std::list<NC_STACK_ypaworld::TTransientVP> *effects, baseRender_msg *arg)
{
    for (auto it = effects->begin(); it != effects->end();)
    {
        if ( !it->vp || (it->lifeTime > 0 && it->age >= it->lifeTime) )
        {
            it = effects->erase(it);
            continue;
        }

        if ( it->followOwner )
        {
            NC_STACK_ypabact *owner = yw_FindLiveBactByGidInList(world->_unitsList, it->followOwnerGid);

            if ( !owner )
            {
                it = effects->erase(it);
                continue;
            }

            if ( it->playerFirstPersonOnly && (!owner->IsPlayerFirstPersonCameraActive() || world->_viewerBact != owner) )
            {
                it->age += arg->frameTime;
                ++it;
                continue;
            }

            if ( it->hideInOwnerMissileCamera &&
                 owner->_bact_type == BACT_TYPES_MISSLE &&
                 world->_viewerBact == owner )
            {
                it->age += arg->frameTime;
                ++it;
                continue;
            }

            // Status FX (damaged/debuff smoke, sparks, electric effects) should follow
            // the unit position, but must not inherit terrain pitch/roll.
            //
            // Important: do NOT clamp attached status FX against owner->_pSector->height.
            // On sloped sectors that value is only a coarse sector height/average, not
            // the exact ground height under the vehicle. Clamping against it randomly
            // yanks smoke/fire far above the hull when a tank climbs/descends terrain,
            // which is the intermittent "floating FX" bug seen in-game.
            if ( it->followUseOwnerTransform )
            {
                it->pos = owner->_position + owner->_rotation.Transpose().Transform(it->followLocalOffset);
                it->rot = owner->_rotation;
            }
            else
            {
                vec3d offset = it->followLocalOffset;
                if ( it->followRotateOffset )
                    offset = owner->_rotation.Transpose().Transform(offset);
                it->pos = owner->_position + offset;
                it->rot = mat3x3::Ident();
            }
        }

        float scale = it->scale > 0.0 ? it->scale : 1.0;
        if ( it->chainFX )
        {
            float t = 1.0;
            if ( it->lifeTime > 0 )
                t = (float)it->age / (float)it->lifeTime;

            if ( t < 0.0 )
                t = 0.0;
            else if ( t > 1.0 )
                t = 1.0;

            scale *= it->startScale + (it->endScale - it->startScale) * t;

            if ( !it->chainBases.empty() )
            {
                int32_t index = (int32_t)(t * (float)it->chainBases.size());
                if ( index >= (int32_t)it->chainBases.size() )
                    index = (int32_t)it->chainBases.size() - 1;

                if ( index != it->chainIndex && it->chainBases[index] )
                {
                    it->vp.reset(it->chainBases[index]->GenRenderInstance());
                    it->chainIndex = index;
                    if ( index < (int32_t)it->chainTints.size() )
                        it->tint = it->chainTints[index];
                    else
                        it->tint = World::TVisualTint();
                }
            }
        }

        vec3d renderScale = it->axisScale * scale;
        mat3x3 renderRot = it->rot.Transpose();
        if ( yw_HasTransientVPRotation(it->localRotation) )
            renderRot *= yw_BuildTransientVPRotationMatrix(it->localRotation);
        if ( World::Spin::HasStrength(it->spin) )
            renderRot *= World::Spin::BuildMatrix(it->spin, it->age);

        it->vp->Bas->TForm().Pos = it->pos;
        it->vp->Bas->TForm().SclRot = renderRot * mat3x3::Scale(renderScale);

        // OpenUA custom VP controls: affect only this transient model and
        // particles emitted by it, then restore defaults for other effects.
        GFX::TGLColor oldTint = arg->tint;
        GFX::TGLColor oldParticleTint = arg->particleTint;
        vec3d oldParticleScale = arg->particleScale;
        vec3d oldParticleSpin = arg->particleSpin;
        float oldParticleLifetimeScale = arg->particleLifetimeScale;
        arg->particleTint = GFX::TGLColor(it->tint.r, it->tint.g, it->tint.b, it->tint.a);
        arg->particleScale = renderScale;
        arg->particleSpin = it->spin;
        arg->particleLifetimeScale = 1.0f;

        bool tinted = !it->tint.IsNeutral();
        if ( tinted )
            arg->tint = GFX::TGLColor(it->tint.r, it->tint.g, it->tint.b, it->tint.a);
        else
            arg->tint = GFX::TGLColor(1.0, 1.0, 1.0, 1.0);

        it->vp->Bas->Render(arg, it->vp.get());

        arg->tint = oldTint;
        arg->particleTint = oldParticleTint;
        arg->particleScale = oldParticleScale;
        arg->particleSpin = oldParticleSpin;
        arg->particleLifetimeScale = oldParticleLifetimeScale;

        it->age += arg->frameTime;
        ++it;
    }
}

void NC_STACK_ypaworld::RenderGame(base_64arg *bs64, int a2)
{
    if ( !_viewerBact )
        return;

    TF::TForm3D *v5 = TF::Engine.GetViewPoint();

    if ( v5 )
        v5->CalcGlobal();

    baseRender_msg rndrs;

    rndrs.flags = (_skyRender && _skyObject) ? GFX::RFLAGS_ALPHA_FOG : 0;
    rndrs.frameTime = bs64->DTime;
    rndrs.globTime = bs64->TimeStamp;
    rndrs.adeCount = 0;

    rndrs.minZ = 1.0;

    if ( _renderSectors == 5 )
        rndrs.maxZ = (float)_normalVizLimit + 100.0;
    else
        rndrs.maxZ = (float)_normalVizLimit + 400.0;

    int v6 = _renderSectors - 1;

    for (int j = 0; j < v6; j++)
    {
        for (int i = 0; i < v6; i++)
        {
            rendering_sectors[j][i].dword4 = 0;
            rendering_sectors[j][i].dword8 = 0;
        }
    }

    int v29 = v6 / 2;
    for (int i = 0; i <= v29; i++)
    {
        int v28 = v29 - i;

        for (int j = -i; j <= i; j++)
        {
            TRenderingSector *sct = &rendering_sectors[v29 + j][v29 - v28];

            RenderAdditionalBeeBox( _viewerBact->_cellId + Common::Point(j, -v28),
                                    sct, &rndrs);

            if ( sct->dword4 )
                RenderSector(sct, &rndrs);

        }

        if ( -v28 != v28 )
        {
            for (int j = -i; j <= i; j++)
            {
                TRenderingSector *sct = &rendering_sectors[v29 + j][v29 + v28];

                RenderAdditionalBeeBox( _viewerBact->_cellId + Common::Point(j, v28),
                                        sct, &rndrs);

                if ( sct->dword4 )
                    RenderSector(sct, &rndrs);
            }
        }
    }


    RenderSuperItems(&rndrs);

    RenderFillers(&rndrs);

    yw_RenderTransientVPs(this, &_transientVPs, &rndrs);

    bs64->field_C = rndrs.adeCount;

    _polysCount = rndrs.adeCount;
    _polysDraw = 7777;


    area_arg_65 rrg;
    rrg.timeStamp = bs64->TimeStamp;
    rrg.frameTime = bs64->DTime;
    rrg.minZ = 1.0;
    rrg.maxZ = rndrs.maxZ;
    rrg.ViewTForm = TF::Engine.GetViewPoint();
    rrg.OwnerTForm = NULL;
    rrg.flags = 0;


    ParticleSystem().UpdateRender(&rrg, bs64->DTime);

    GFX::Engine.BeginScene();

    if ( _skyRender )
        yw_renderSky(&rndrs);

    GFX::Engine.Rasterize();

    GFX::Engine.EndScene();

    if ( a2 )
    {
        uint32_t tpm = profiler_begin();
        sb_0x4d7c08__sub0(this);
        _profileVals[PFID_NEWGUITIME] = profiler_end(tpm);
    }
}


void NC_STACK_ypaworld::ResetAccumMap()
{
    for( int y = 0; y < _mapSize.y; y++ )
    {
        for( int x = 0; x < _mapSize.x; x++ )
        {
            EnergyAccum &accum = _energyAccumMap(x, y);
            accum.Energy = 0;
            accum.Owner = _cells(x, y).owner;
        }
    }

    _nextPSForUpdate = 0;
}

void NC_STACK_ypaworld::SetupPowerStationInfo(cellArea *cell, int power, int buildingId)
{
    if (!cell) return;

    TPowerStationInfo &ps = _powerStations[cell->Id];

    ps.CellId = cell->CellId;
    ps.Power = power;
    ps.EffectivePower = power;
    ps.pCell = cell;

    cell->PurposeType = cellArea::PT_POWERSTATION;
    cell->PurposeIndex = buildingId;

    ResetAccumMap();
}


void NC_STACK_ypaworld::sb_0x456384(const Common::Point &cellId, int ownerid2, int blg_id, int a7)
{
    uamessage_bldVhcl bvMsg;

    cellArea &cell = _cells(cellId);
    World::TBuildingProto *bld = &_buildProtos[ blg_id ];
    TSectorDesc *sectp = &_secTypeArray[ bld->SecType ];

    int v43 = 1;

    NC_STACK_yparobo *robo = NULL;

    if ( cell.IsGamePlaySector() )
    {
        _lvlBuildingsMap(cellId) = blg_id;
        _lvlTypeMap(cellId) = bld->SecType;

        cell.type_id = bld->SecType;
        cell.energy_power = 0;
        cell.PurposeType = cellArea::PT_BUILDINGS;
        cell.SectorType = sectp->SectorType;
        cell.PurposeIndex = blg_id;
        cell.DecorationFX = bld->DecorationFX;
        cell.DecorationFXNextTime = 0;
        cell.DecorationFXPersistentId = 0;

        int v49;

        if ( sectp->SectorType == 1 )
        {
            cell.buildings_health.fill(0);

            v49 = 1;
        }
        else
        {
            v49 = 3;
        }

        for (int yy = 0; yy < v49; yy++)
        {
            for (int xx = 0; xx < v49; xx++)
                cell.buildings_health.At(xx, yy) = sectp->SubSectors.At(xx, yy)->StartHealth;
        }

        if ( bld->ModelID == 1 )
            SetupPowerStationInfo(&cell, bld->Power, blg_id);

        CellSetOwner(&cell, ownerid2);

        // Building spawner runtime ownership must be tied to the building that
        // has just been created, not to a previous owner stored when the sector
        // was conquered before construction.
        if ( bld->spawn_units )
        {
            cell.BuildingSpawnInitialOwner = ownerid2;
            cell.BuildingSpawnLastTime = 0;
            cell.BuildingSpawnedGids.clear();
        }
        else
        {
            cell.BuildingSpawnInitialOwner = 0;
            cell.BuildingSpawnLastTime = 0;
            cell.BuildingSpawnedGids.clear();
        }

        for( NC_STACK_ypabact * &unit: _unitsList )
        {
            if (unit->_bact_type == BACT_TYPES_ROBO && unit->_owner == ownerid2)
            {
                robo = dynamic_cast<NC_STACK_yparobo *>(unit);
                break;
            }
        }

        if ( _isNetGame )
        {
            if ( robo != _userRobo )
                v43 = 0;
        }

        if ( !a7 )
        {
            if ( robo && robo->_status != BACT_STATUS_DEAD && v43 )
            {
                NC_STACK_ypagun *commander = NULL;

                int v39 = robo->getROBO_commCount();

                v39++;

                robo->setROBO_commCount(v39);

                for ( size_t i = 0; i < bld->Guns.size(); i++)
                {
                    World::TBuildingProto::TGun &GunProto = bld->Guns[i];

                    if ( !GunProto.VhclID )
                        break;

                    ypaworld_arg146 v33;
                    v33.vehicle_id = GunProto.VhclID;
                    v33.pos = GunProto.Pos + World::SectorIDToCenterPos3( cellId );

                    NC_STACK_ypabact *gun_obj = ypaworld_func146(&v33);
                    NC_STACK_ypagun *gunn = dynamic_cast<NC_STACK_ypagun *>(gun_obj);

                    if ( gun_obj )
                    {
                        gun_obj->_owner = ownerid2;

                        if (gunn)
                            gunn->ypagun_func128(GunProto.Dir, false);

                        setState_msg v34;
                        v34.newStatus = BACT_STATUS_CREATE;
                        v34.unsetFlags = 0;
                        v34.setFlags = 0;

                        gunn->SetStateInternal(&v34);

                        gun_obj->_scale_time = 500;
                        gun_obj->_scale = vec3d(1.0, 1.0, 1.0);

                        gun_obj->_host_station = robo;
                        gun_obj->_commandID = v39;

                        if ( _isNetGame && i < 8 )
                        {
                            gun_obj->_gid |= ownerid2 << 24;

                            bvMsg.vhcl[i].id = gun_obj->_gid;
                            bvMsg.vhcl[i].base = GunProto.Dir;
                            bvMsg.vhcl[i].pos = gun_obj->_position;
                            bvMsg.vhcl[i].protoID = GunProto.VhclID;
                        }

                        if ( commander )
                        {
                            commander->AddSubject(gunn);
                        }
                        else
                        {
                            commander = gunn;

                            robo->AddSubject(gunn);
                        }
                    }
                }

                if ( _isNetGame )
                {
                    bvMsg.msgID = UAMSG_BUILDINGVHCL;
                    bvMsg.tstamp = _timeStamp;
                    bvMsg.owner = ownerid2;

                    NetBroadcastMessage(&bvMsg, sizeof(bvMsg), true);
                }
            }
        }
    }
}


void NC_STACK_ypaworld::DestroyAllGunsInSector(cellArea *cell)
{
    /*
     * Destroy all GUN units in sector
     */

    // Safe iterator, because it will call ModifyEnergy->Die for units
    for ( NC_STACK_ypabact* itUnit : cell->unitsList.safe_iter() )
    {
        int v5 = 0;

        if ( _isNetGame )
        {
            // In netgame only destroy own units
            if ( itUnit->_owner == _userUnit->_owner )
            {
                if ( itUnit->_status != BACT_STATUS_DEAD && itUnit->_status != BACT_STATUS_BEAM && itUnit->_status != BACT_STATUS_CREATE )
                {
                    if ( itUnit->_bact_type == BACT_TYPES_GUN )
                    {
                        NC_STACK_ypagun *gun = dynamic_cast<NC_STACK_ypagun *>( itUnit );

                        if (!gun->IsRoboGun())
                            v5 = 1;
                    }
                }
            }
        }
        else
        {
            if ( itUnit->_status != BACT_STATUS_DEAD && itUnit->_status != BACT_STATUS_BEAM && itUnit->_status != BACT_STATUS_CREATE )
            {
                if ( itUnit->_bact_type == BACT_TYPES_GUN )
                {
                    NC_STACK_ypagun *gun = dynamic_cast<NC_STACK_ypagun *>( itUnit );

                    if (!gun->IsRoboGun())
                        v5 = 1;
                }
            }
        }

        if ( v5 )
        {
            bact_arg84 v8;
            v8.energy = -22000000;
            v8.unit = NULL;

            itUnit->ModifyEnergy(&v8);
        }
    }
}

bool NC_STACK_ypaworld::BuildingConstructBegin(cellArea *cell, uint8_t buildID, int owner, int cTime)
{
    if (!cell)
        return false;

    if (cell->IsBorder())
        return false;

    auto it = _inBuildProcess.find(cell->Id);
    if (it != _inBuildProcess.end())
        return false;

    TConstructInfo &bldProc = _inBuildProcess[cell->Id];
    bldProc.CellID = cell->CellId;
    bldProc.Time = 0;
    bldProc.EndTime = cTime;
    bldProc.BuildID = buildID;
    bldProc.Owner = owner;

    cell->PurposeType = cellArea::PT_CONSTRUCTING;

    for (NC_STACK_ypabact * &unit : _unitsList)
    {
        if ( unit->_bact_type == BACT_TYPES_ROBO && owner == unit->_owner )
        {
            SFXEngine::SFXe.startSound(&unit->_soundcarrier, 11);
            break;
        }
    }

    return true;
}

int ypaworld_func137__sub0__sub0(UAskeleton::Data *skl, int id, const vec3d &v, float r, vec3d &out)
{
    UAskeleton::Polygon &pg = skl->polygons[id];
    vec3d tmp(0.0, 0.0, 0.0);

    for (int i = 0; i < pg.num_vertices; i++)
    {
        int16_t idd = pg.v[i];
        tmp += static_cast<vec3d> (skl->POO[ idd ]);
    }

    vec3d tmp2 = tmp / pg.num_vertices - v;

    float v26 = tmp2.length();

    if ( v26 <= r )
        return 0;

    out = tmp2 * (r / v26) + v;

    return 1;
}

void NC_STACK_ypaworld::ypaworld_func137__sub0(ypaworld_arg137 *arg, const TSectorCollision &a2)
{
    for (size_t i = 0; i < a2.sklt->GetSkelet()->polygons.size(); i++)
    {
        const UAskeleton::Polygon &tria = a2.sklt->GetSkelet()->polygons[i];

        vec3d t0 = tria.Normal();

        float v9 = t0.dot( arg->pos2 );

        if ( v9 > 0.0 )
        {
            float v26 = -( t0.dot( arg->pos ) + tria.D) / ( t0.dot( t0 ) * arg->radius);

            if ( v26 > 0.0 && v26 <= 1.0 )
            {
                vec3d tx = arg->pos + t0 * (arg->radius * v26);

                int v27 = 0;

                vec3d v18;

                if ( ypaworld_func137__sub0__sub0(a2.sklt->GetSkelet(), i, tx, arg->radius, v18) )
                {
                    if ( sub_44D36C(v18, i, a2.sklt) )
                        v27 = 1;
                }
                else
                    v27 = 1;

                if ( v27 )
                {
                    if ( arg->coll_count < arg->coll_max )
                    {
                        int pos = arg->coll_count;

                        arg->collisions[pos].pos1 = a2.pos + tx;
                        arg->collisions[pos].pos2 = tria.Normal();

                        arg->coll_count++;
                    }
                }
            }
        }
    }
}

NC_STACK_ypabact * NC_STACK_ypaworld::FindBactByCmdOwn(uint32_t commandID, char owner)
{
    for ( NC_STACK_ypabact * &robo : _unitsList )
    {
        if ( robo->_bact_type == BACT_TYPES_ROBO && robo->_owner == owner)
        {
            if ( robo->_commandID == commandID )
            {
                if ( robo->_status == BACT_STATUS_DEAD )
                    return NULL;
                else
                    return robo;
            }
            else
            {
                for ( NC_STACK_ypabact * &unit : robo->_kidList )
                {
                    if ( unit->_commandID == commandID )
                    {
                        if ( unit->_status == BACT_STATUS_DEAD )
                            return NULL;
                        else
                            return unit;
                    }
                }
            }
        }
    }

    return NULL;
}


void NC_STACK_ypaworld::BuildingConstructUpdate(int dtime)
{
    for(auto it  = _inBuildProcess.begin(); it != _inBuildProcess.end(); )
    {
        TConstructInfo &bldProc = it->second;
        bldProc.Time += dtime;

        if ( bldProc.Time >= bldProc.EndTime )
        {
            cellArea &rCell = _cells( bldProc.CellID );
            rCell.PurposeType = cellArea::PT_NONE;
            rCell.PurposeIndex = 0;

            sb_0x456384(bldProc.CellID, bldProc.Owner, bldProc.BuildID, 0);

            if ( bldProc.Owner == _userRobo->_owner )
            {
                if ( _buildProtos[ bldProc.BuildID ].ModelID )
                {
                    yw_arg159 arg159;

                    arg159.unit = _userRobo;
                    arg159.Priority = 65;

                    switch( _buildProtos[ bldProc.BuildID ].ModelID )
                    {
                        case 1:
                        arg159.MsgID = 36;
                        break;

                        case 2:
                        arg159.MsgID = 38;
                        break;

                        case 3:
                        arg159.MsgID = 37;
                        break;

                        default:
                        arg159.MsgID = 0;
                        break;
                    }

                    ypaworld_func159(&arg159);
                }
            }
            it = _inBuildProcess.erase(it);
        }
        else
            ++it;
    }
}

void NC_STACK_ypaworld::BuildingDecorationFXUpdate()
{
    for (cellArea &cell : _cells)
    {
        if ( !yw_IsActiveBuildingSpawnerCell(cell) )
        {
            RemoveTransientVP(cell.DecorationFXPersistentId);
            cell.DecorationFXPersistentId = 0;
            cell.DecorationFXNextTime = 0;
            cell.BuildingSpawnLastTime = 0;
            cell.BuildingSpawnedGids.clear();
            continue;
        }

        UpdateDecorationFX(cell.DecorationFX, cell.DecorationFXNextTime, cell.CenterPos, &cell.DecorationFXPersistentId);

        if ( cell.PurposeIndex >= 0 && (size_t)cell.PurposeIndex < _buildProtos.size() )
            yw_UpdateBuildingSpawner(this, cell, _buildProtos[cell.PurposeIndex]);
    }
}

bool NC_STACK_ypaworld::IsAnyBuildingProcess(int owner) const
{
    for(const auto &it : _inBuildProcess)
    {
        if (it.second.Owner == owner)
            return true;
    }
    return false;
}

void NC_STACK_ypaworld::ypaworld_func64__sub6__sub0()
{
    for(int i = 0; i < 8; i++)
        _countUnitsPerOwner[i] = 0;

    for ( NC_STACK_ypabact * &robo : _unitsList )
    {
        if ( robo->_bact_type == BACT_TYPES_ROBO && robo->_status != BACT_STATUS_DEAD && robo->_status != BACT_STATUS_BEAM )
        {
            for( NC_STACK_ypabact * &comnd : robo->_kidList )
            {
                if ( comnd->_status != BACT_STATUS_DEAD && comnd->_status != BACT_STATUS_BEAM )
                {
                    if ( IsSpectatorBact(comnd) )
                        continue;

                    bool a4 = false;

                    if ( comnd->_bact_type == BACT_TYPES_GUN )
                    {
                        NC_STACK_ypagun *gun = dynamic_cast<NC_STACK_ypagun *>( comnd );
                        a4 = gun->IsRoboGun();
                    }

                    if ( !a4 )
                    {
                        _countUnitsPerOwner[ comnd->_owner ]++;

                        for( NC_STACK_ypabact * &unit : comnd->_kidList )
                        {
                            if ( unit->_status != BACT_STATUS_DEAD && unit->_status != BACT_STATUS_BEAM && !IsSpectatorBact(unit) )
                                _countUnitsPerOwner[ unit->_owner ]++;
                        }
                    }
                }
            }
        }
    }
}


void ypaworld_func64__sub6(NC_STACK_ypaworld *yw)
{
    int v13[8];

    for (int i = 0; i < 8; i++)
        v13[i] = 0;

    for (const auto &ps : yw->_powerStations)
    {
        if (ps.second.pCell)
            v13[ ps.second.pCell->owner ] += ps.second.EffectivePower;
    }

    yw->ypaworld_func64__sub6__sub0();

    for (int i = 0; i < 8; i++)
    {
        v13[i] /= 2;

        if ( v13[i] <= 0 )
        {
            yw->_reloadRatioPositive[i] = 0;
            yw->_reloadRatioClamped[i] = 0;
        }
        else
        {
            int v15 = yw->_countSectorsPerOwner[i];

            if ( v15 < 0 )
                v15 = 0;

            yw->_reloadRatioClamped[i] = (float)v15 / (float)v13[i];
            yw->_reloadRatioPositive[i] = (float)v15 / (float)v13[i];

            if ( yw->_isNetGame )
            {
                if ( yw->_levelUnitLimitType == 1 )
                {
                    int v16 = yw->_countUnitsPerOwner[yw->_userRobo->_owner] - yw->_levelUnitLimit;

                    if ( v16 > 0 )
                    {
                        int v10 = (float)yw->_levelUnitLimitArg * 0.01 * (float)v16;

                        yw->_reloadRatioClamped[i] -= v10;
                        yw->_reloadRatioPositive[i] -= v10;
                    }
                }
            }

            if ( yw->_reloadRatioClamped[i] > 1.0 )
                yw->_reloadRatioClamped[i] = 1.0;
            else if ( yw->_reloadRatioClamped[i] < 0.0 )
                yw->_reloadRatioClamped[i] = 0;

            if ( yw->_reloadRatioPositive[i] < 0.0 )
                yw->_reloadRatioPositive[i] = 0;
        }
    }
}


void NC_STACK_ypaworld::RecalcSectorsPowerForPS(const TPowerStationInfo &ps)
{
    int pwrTmp = ps.EffectivePower;
    int powsCount = 0;

    while (pwrTmp > 0)
    {
        pwrTmp >>= 1;
        powsCount++;
    }

    int sdx = -powsCount;
    int edx = powsCount + 1;

    int sdy = -powsCount;
    int edy = powsCount + 1;

    if ( ps.CellId.x + sdx < 1 )
        sdx = 1 - ps.CellId.x;

    if ( ps.CellId.y + sdy < 1 )
        sdy = 1 - ps.CellId.y;

    if ( ps.CellId.x + edx >= _mapSize.x )
        edx = _mapSize.x - ps.CellId.x - 1;

    if ( ps.CellId.y + edy >= _mapSize.y )
        edy = _mapSize.y - ps.CellId.y - 1;

    for (int dy = sdy; dy < edy; dy++)
    {
        for (int dx = sdx; dx < edx; dx++)
        {
            int v17 = ps.EffectivePower  >>  _sqrtTable(abs(dx), abs(dy));

            EnergyAccum &accum = _energyAccumMap(dx + ps.CellId.x, dy + ps.CellId.y);

            if ( accum.Owner == ps.pCell->owner )
            {
                accum.Energy += v17; // Add power to this cell

                if ( accum.Energy > 255 )
                    accum.Energy = 255;
            }

        }
    }
}

static bool yw_IsValidMobilePowerGenerator(NC_STACK_ypaworld *yw, NC_STACK_ypabact *unit)
{
    if ( !yw ||
         !unit ||
         unit->_owner == World::OWNER_0 ||
         unit->_status == BACT_STATUS_DEAD ||
         unit->_status == BACT_STATUS_CREATE ||
         unit->_status == BACT_STATUS_BEAM ||
         (unit->_status_flg & (BACT_STFLAG_DEATH1 | BACT_STFLAG_DEATH2)) ||
         unit->_bact_type == BACT_TYPES_MISSLE ||
         unit->_bact_type == BACT_TYPES_ROBO ||
         unit->_bact_type == BACT_TYPES_GUN ||
         (size_t)(unit->_mimic_disguise_vehicleID ? unit->_mimic_disguise_vehicleID : unit->_vehicleID) >= yw->_vhclProtos.size() )
        return false;

    uint8_t protoId = unit->_mimic_disguise_vehicleID ? unit->_mimic_disguise_vehicleID : unit->_vehicleID;
    const World::TVhclProto &proto = yw->_vhclProtos[protoId];
    return proto.power > 0 && proto.power_radius > 0.0;
}

bool NC_STACK_ypaworld::IsValidMobilePowerGenerator(NC_STACK_ypabact *unit)
{
    return yw_IsValidMobilePowerGenerator(this, unit);
}

static void yw_AddMobilePowerInfluenceFromGenerator(NC_STACK_ypaworld *yw,
                                                    NC_STACK_ypabact *target,
                                                    NC_STACK_ypabact *generator,
                                                    TMobilePowerInfluence &influence)
{
    if ( !yw || !target || !generator )
        return;

    if ( yw_IsValidMobilePowerGenerator(yw, generator) )
    {
        uint8_t protoId = generator->_mimic_disguise_vehicleID ? generator->_mimic_disguise_vehicleID : generator->_vehicleID;
        const World::TVhclProto &proto = yw->_vhclProtos[protoId];

        float dx = target->_position.x - generator->_position.x;
        float dz = target->_position.z - generator->_position.z;
        float distSq = dx * dx + dz * dz;
        float radiusSq = proto.power_radius * proto.power_radius;

        if ( distSq <= radiusSq )
        {
            float dist = sqrt(distSq);
            float factor = 1.0 - (dist / proto.power_radius);
            if ( factor < 0.25 )
                factor = 0.25;

            int addPower = (int)(proto.power * factor + 0.5);
            if ( addPower > 0 )
            {
                float addEnergyPower = addPower;

                if ( generator->_owner == target->_owner )
                {
                    influence.AlliedPower += addPower;
                    if ( influence.AlliedPower > 255 )
                        influence.AlliedPower = 255;

                    influence.AlliedEnergyPower += addEnergyPower;
                    if ( influence.AlliedEnergyPower > 255.0 )
                        influence.AlliedEnergyPower = 255.0;
                }
                else
                {
                    influence.EnemyPower += addPower;
                    if ( influence.EnemyPower > 255 )
                        influence.EnemyPower = 255;

                    influence.EnemyEnergyPower += addEnergyPower;
                    if ( influence.EnemyEnergyPower > 255.0 )
                        influence.EnemyEnergyPower = 255.0;
                }
            }
        }
    }

    for (NC_STACK_ypabact *kid : generator->_kidList)
        yw_AddMobilePowerInfluenceFromGenerator(yw, target, kid, influence);
}

TMobilePowerInfluence NC_STACK_ypaworld::FindMobilePowerInfluenceForUnit(NC_STACK_ypabact *target)
{
    TMobilePowerInfluence influence;

    if ( !target ||
         target->_owner == World::OWNER_0 ||
         target->_status == BACT_STATUS_DEAD ||
         target->_status == BACT_STATUS_CREATE ||
         target->_status == BACT_STATUS_BEAM ||
         (target->_status_flg & (BACT_STFLAG_DEATH1 | BACT_STFLAG_DEATH2)) ||
         target->_bact_type == BACT_TYPES_MISSLE )
        return influence;

    for (NC_STACK_ypabact *generator : _unitsList)
        yw_AddMobilePowerInfluenceFromGenerator(this, target, generator, influence);

    return influence;
}

void NC_STACK_ypaworld::AddMobileVehiclePowerToAccumMap()
{
    // Mobile power is owner-aware, so gameplay is applied per target unit in
    // EnergyInteract instead of through cell.energy_power/cell.owner.
}

void NC_STACK_ypaworld::DoSectorsEnergyRecalc()
{
    // Recompute power on sectors
    if ( !_powerStations.empty() ) // If we have powerstations
    {
        auto itPs = _powerStations.lower_bound(_nextPSForUpdate);

        if (itPs == _powerStations.end()) // If we reach end of power stations list, apply power to sectors
        {
            AddMobileVehiclePowerToAccumMap();
            UpdatePowerEnergy(); // Apply power to sectors and clean power matrix for next compute iteration.
        }
        else
        {
            if ( itPs->second.EffectivePower ) // if this power station has power
                RecalcSectorsPowerForPS(itPs->second); // Add power to power matrix

            _nextPSForUpdate = itPs->first + 1; // go to next station in next update loop
        }
    }
    else
    {
        AddMobileVehiclePowerToAccumMap();
        UpdatePowerEnergy();
    }
}


void NC_STACK_ypaworld::sub_4D12D8(int id, int a3)
{
    TMapSuperItem &sitem = _levelInfo.SuperItems[id];

    sitem.State = TMapSuperItem::STATE_ACTIVE;
    sitem.TriggerTime = 0;
    sitem.ActivateOwner = sitem.PCell->owner;

    if ( !a3 )
    {
        sitem.ActiveTime = _timeStamp;
        sitem.LastTenSec = 0;
        sitem.LastSec = 0;
        sitem.CountDown = sitem.TimerValue;
    }

    ypaworld_arg148 arg148;
    arg148.owner = sitem.PCell->owner;
    arg148.blg_ID = sitem.ActiveBldID;
    arg148.field_C = 1;
    arg148.CellId = sitem.CellId;
    arg148.field_18 = 0;

    ypaworld_func148(&arg148);

    sitem.PCell->PurposeType = cellArea::PT_STOUDSON;
    sitem.PCell->PurposeIndex = id;

    yw_arg159 arg159;
    arg159.unit = 0;
    arg159.Priority = 94;

    if ( sitem.Type == TMapSuperItem::TYPE_BOMB )
    {
        arg159.txt = Locale::Text::Feedback(Locale::FEEDBACK_BOMBACT);
        arg159.MsgID = 70;
    }
    else if ( sitem.Type == TMapSuperItem::TYPE_WAVE )
    {
        arg159.txt = Locale::Text::Feedback(Locale::FEEDBACK_WAVEACT);
        arg159.MsgID = 74;
    }
    else
    {
        arg159.MsgID = 0;
        arg159.txt = "Cant happen.";
    }

    ypaworld_func159(&arg159);
}

void NC_STACK_ypaworld::sub_4D1594(int id)
{
    TMapSuperItem &sitem = _levelInfo.SuperItems[id];

    sitem.State = TMapSuperItem::STATE_STOPPED;

    ypaworld_arg148 arg148;
    arg148.owner = sitem.PCell->owner;
    arg148.blg_ID = sitem.InactiveBldID;
    arg148.field_C = 1;
    arg148.CellId = sitem.CellId;
    arg148.field_18 = 0;

    ypaworld_func148(&arg148);

    sitem.PCell->PurposeType = cellArea::PT_STOUDSON;
    sitem.PCell->PurposeIndex = id;

    yw_arg159 arg159;
    arg159.unit = 0;
    arg159.Priority = 93;

    if ( sitem.Type == TMapSuperItem::TYPE_BOMB )
    {
        arg159.txt = Locale::Text::Feedback(Locale::FEEDBACK_BOMBFROZ);
        arg159.MsgID = 72;
    }
    else if ( sitem.Type == TMapSuperItem::TYPE_WAVE )
    {
        arg159.txt = Locale::Text::Feedback(Locale::FEEDBACK_WAVEFROZ);
        arg159.MsgID = 76;
    }
    else
    {
        arg159.MsgID = 0;
        arg159.txt = "Cant happen.";
    }

    ypaworld_func159(&arg159);
}

void NC_STACK_ypaworld::sub_4D1444(int id)
{
    TMapSuperItem &sitem = _levelInfo.SuperItems[id];
    sitem.State = TMapSuperItem::STATE_TRIGGED;
    sitem.TriggerTime = _timeStamp;

    ypaworld_arg148 arg148;
    arg148.owner = sitem.PCell->owner;
    arg148.blg_ID = sitem.TriggerBldID;
    arg148.field_C = 1;
    arg148.CellId = sitem.CellId;
    arg148.field_18 = 0;

    ypaworld_func148(&arg148);

    sitem.PCell->PurposeType = cellArea::PT_STOUDSON;
    sitem.PCell->PurposeIndex = id;

    sitem.LastRadius = 0;

    yw_arg159 arg159;
    arg159.Priority = 95;
    arg159.unit = 0;

    if ( sitem.Type == TMapSuperItem::TYPE_BOMB )
    {
        arg159.txt = Locale::Text::Feedback(Locale::FEEDBACK_BOMBTRIG);
        arg159.MsgID = 71;
    }
    else if ( sitem.Type == TMapSuperItem::TYPE_WAVE )
    {
        arg159.txt = Locale::Text::Feedback(Locale::FEEDBACK_WAVETRIG);
        arg159.MsgID = 75;
    }
    else
    {
        arg159.MsgID = 0;
        arg159.txt = "Cant happen.";
    }

    ypaworld_func159(&arg159);
}


NC_STACK_ypabact * NC_STACK_ypaworld::sb_0x47b028__sub0(uint32_t bactid)
{
    for ( NC_STACK_ypabact * &station : _unitsList )
    {
        if ( bactid == station->_gid )
            return station;

        for ( NC_STACK_ypabact * &commander : station->_kidList )
        {
            if ( bactid == commander->_gid )
                return station; // CHECK IT

            for ( NC_STACK_ypabact * &slave : commander->_kidList )
            {
                if ( bactid == slave->_gid )
                    return station;  // CHECK IT
            }
        }
    }

    return NULL;
}

void NC_STACK_ypaworld::RefreshUnitPRT(NC_STACK_ypabact *unit, NC_STACK_ypabact *robo, bool isRobo)
{
    bact_arg80 arg80;
    arg80.pos = unit->_position;

    if ( unit->_bact_type == BACT_TYPES_GUN )
        arg80.field_C = 4;
    else
        arg80.field_C = 0;

    unit->SetPosition(&arg80);

    if ( unit->_bact_type == BACT_TYPES_GUN )
    {
        NC_STACK_ypagun *guno = (NC_STACK_ypagun *)unit;

        guno->ypagun_func128(guno->_gunBasis, false);
    }

    setState_msg arg78;
    arg78.newStatus = unit->_status;
    arg78.setFlags = 0;
    arg78.unsetFlags = 0;
    unit->SetState(&arg78);

    if ( unit->_status_flg & BACT_STFLAG_DEATH2 )
    {
        arg78.newStatus = BACT_STATUS_NOPE;
        arg78.unsetFlags = 0;
        arg78.setFlags = BACT_STFLAG_DEATH2;
        unit->SetState(&arg78);
    }

    if ( unit->_status_flg & BACT_STFLAG_FIRE )
    {
        arg78.newStatus = BACT_STATUS_NOPE;
        arg78.unsetFlags = 0;
        arg78.setFlags = BACT_STFLAG_FIRE;
        unit->SetState(&arg78);
    }

    if ( !isRobo )
    {
        unit->_host_station = dynamic_cast<NC_STACK_yparobo *>(robo);
        unit->_owner = robo->_owner;
    }

    if ( unit->_primTtype == BACT_TGT_TYPE_UNIT )
    {
        unit->_primTtype = BACT_TGT_TYPE_NONE;

        setTarget_msg arg67;
        arg67.tgt.pbact = sb_0x47b028__sub0((int)(size_t)unit->_primT.pbact);
        arg67.tgt_type = BACT_TGT_TYPE_UNIT_IND;
        arg67.priority = 0;
        unit->SetTarget(&arg67);
    }

    if ( unit->_primTtype == BACT_TGT_TYPE_CELL )
    {
        unit->_primTtype = BACT_TGT_TYPE_NONE;

        setTarget_msg arg67_1;
        arg67_1.tgt_type = BACT_TGT_TYPE_CELL_IND;
        arg67_1.tgt_pos = unit->_primTpos;
        arg67_1.priority = 0;

        unit->SetTarget(&arg67_1);
    }
}


int ypaworld_func64__sub4(NC_STACK_ypaworld *yw, base_64arg *arg)
{
    if ( yw->_isNetGame )
        return 0;

    if ( !yw->_gamePaused )
    {
        if ( arg->field_8->HotKeyID == 32 || arg->field_8->KbdLastHit == Input::KC_PAUSE )
        {
            yw->_gamePaused = true;
            yw->_gamePausedTimeStamp = arg->TimeStamp;
        }
        return 0;
    }

    if ( arg->field_8->KbdLastHit != Input::KC_NONE )
    {
        yw->_gamePaused = false;
        arg->TimeStamp = yw->_gamePausedTimeStamp;
    }
    else
    {
        GFX::Engine.BeginFrame();

        /*yw->_win3d->setRSTR_BGpen(0);

        yw->_win3d->raster_func192(NULL);*/

        vec3d a2a = yw->_viewerPosition + vec3d::OY(50000.0);

        SFXEngine::SFXe.sub_423EFC(1, a2a, vec3d(0.0), mat3x3::Ident());

        if ( arg->TimeStamp / 500 & 1 )
        {
            const std::string v6 = Locale::Text::Common(Locale::CMN_PAUSED);

            CmdStream v10;
            v10.reserve(256);

            FontUA::select_tileset(&v10, 15);

            FontUA::set_xpos(&v10, 0);
            FontUA::set_center_ypos(&v10, -yw->_fontH / 2);

            FontUA::FormateCenteredSkipableItem(yw->_guiTiles[15], &v10, v6, yw->_screenSize.x);

            FontUA::set_end(&v10);

            GFX::Engine.ProcessDrawSeq(v10);
        }

        SFXEngine::SFXe.sb_0x424c74();

        GFX::Engine.EndFrame();
    }
    return 1;
}


void ypaworld_func64__sub2(NC_STACK_ypaworld *yw)
{
    yw->_playerInHSGun = false;

    if ( yw->_userRobo != yw->_userUnit )
    {
        NC_STACK_yparobo *robo = dynamic_cast<NC_STACK_yparobo *>(yw->_userRobo);

        for (World::TRoboGun &gun : robo->GetGuns())
        {
            if ( yw->_userUnit == gun.gun_obj )
                yw->_playerInHSGun = true;
        }
    }
}


void ypaworld_func64__sub9(NC_STACK_ypaworld *yw)
{
    for ( size_t i = 0; i < yw->_levelInfo.Gates.size(); i++ )
    {
        const TMapGate &gate = yw->_levelInfo.Gates[i];
        int gateState = cellArea::PT_GATEOPENED;

        if ( gate.PCell->owner == yw->_userRobo->_owner )
        {
            for( const TMapKeySector &ks : gate.KeySectors )
            {
                if (ks.PCell)
                {
                    if (ks.PCell->owner != yw->_userRobo->_owner)
                    {
                        gateState = cellArea::PT_GATECLOSED;
                        break;
                    }
                }
            }
        }
        else
        {
            gateState = cellArea::PT_GATECLOSED;
        }

        if ( gate.PCell->PurposeType != gateState )
        {
            ypaworld_arg148 arg148;
            arg148.owner = gate.PCell->owner;
            arg148.field_C = 1;
            arg148.CellId = gate.CellId;
            arg148.field_18 = 0;

            if ( gateState == cellArea::PT_GATEOPENED )
            {
                arg148.blg_ID = gate.OpenBldID;
            }
            else
            {
                arg148.blg_ID = gate.ClosedBldID;

                yw_arg159 arg159;
                arg159.unit = 0;
                arg159.Priority = 65;
                arg159.txt = Locale::Text::Feedback(Locale::FEEDBACK_GATECLOSE);
                arg159.MsgID = 24;

                yw->ypaworld_func159(&arg159);
            }

            yw->ypaworld_func148(&arg148);

            gate.PCell->PurposeType = gateState;
            gate.PCell->PurposeIndex = i;
        }

        if ( gateState == cellArea::PT_GATEOPENED )
        {
            int energ = 0;

            for ( NC_STACK_ypabact* &v8 : gate.PCell->unitsList )
            {
                if ( v8->_status != BACT_STATUS_DEAD && v8->_status != BACT_STATUS_BEAM )
                {
                    if ( v8->_bact_type != BACT_TYPES_ROBO && v8->_bact_type != BACT_TYPES_MISSLE && v8->_bact_type != BACT_TYPES_GUN )
                        energ += (v8->_energy_max + 99) / 100;
                }
            }

            if ( energ <= yw->_beamEnergyCapacity )
            {
                if ( yw->_timeStamp - yw->_msgTimestampGates > 60000 )
                {
                    yw_arg159 arg159_1;
                    arg159_1.unit = 0;
                    arg159_1.Priority = 49;
                    arg159_1.txt = Locale::Text::Feedback(Locale::FEEDBACK_GATEOPEN);
                    arg159_1.MsgID = 23;

                    yw->ypaworld_func159(&arg159_1);
                    yw->_msgTimestampGates = yw->_timeStamp;
                }
            }
            else
            {
                if ( yw->_timeStamp - yw->_msgTimestampGates > 40000 )
                {
                    yw_arg159 arg159_2;
                    arg159_2.unit = 0;
                    arg159_2.Priority = 10;
                    arg159_2.txt = Locale::Text::Feedback(Locale::FEEDBACK_GATEFULL);
                    arg159_2.MsgID = 46;

                    yw->ypaworld_func159(&arg159_2);
                    yw->_msgTimestampGates = yw->_timeStamp;
                }
            }
        }
    }
}


bool NC_STACK_ypaworld::sub_4D11C0(int id, int owner)
{
    const TMapSuperItem &sitem = _levelInfo.SuperItems[id];

    if ( sitem.PCell->owner != owner )
        return false;

    if ( sitem.KeySectors.empty() )
        return true;

    for ( const TMapKeySector &ks : sitem.KeySectors )
    {
        if ( ks.PCell->owner != owner )
            return false;
    }
    return true;
}

bool NC_STACK_ypaworld::sub_4D12A0(int owner)
{
    for ( NC_STACK_ypabact * &unit : _unitsList )
    {
        if ( unit->_bact_type == BACT_TYPES_ROBO && owner == unit->_owner )
            return true;
    }

    return false;
}

void NC_STACK_ypaworld::sub_4D16C4(int id)
{
    TMapSuperItem &sitem = _levelInfo.SuperItems[id];

    sitem.State = TMapSuperItem::STATE_INACTIVE;
    sitem.ActiveTime = 0;
    sitem.TriggerTime = 0;
    sitem.ActivateOwner = 0;
    sitem.CountDown = 0;

    ypaworld_arg148 arg148;
    arg148.owner = sitem.PCell->owner;
    arg148.blg_ID = sitem.InactiveBldID;
    arg148.field_C = 1;
    arg148.CellId = sitem.CellId;
    arg148.field_18 = 0;

    ypaworld_func148(&arg148);

    sitem.PCell->PurposeType = cellArea::PT_STOUDSON;
    sitem.PCell->PurposeIndex = id;

    yw_arg159 arg159;
    arg159.unit = NULL;
    arg159.Priority = 92;

    if ( sitem.Type == TMapSuperItem::TYPE_BOMB )
    {
        arg159.txt = Locale::Text::Feedback(Locale::FEEDBACK_BOMBDEACT);
        arg159.MsgID = 73;
    }
    else if ( sitem.Type == TMapSuperItem::TYPE_WAVE )
    {
        arg159.txt = Locale::Text::Feedback(Locale::FEEDBACK_WAVEDEACT);
        arg159.MsgID = 77;
    }
    else
    {
        arg159.MsgID = 0;
        arg159.txt = "Cant happen.";
    }

    ypaworld_func159(&arg159);
}

void NC_STACK_ypaworld::ypaworld_func64__sub19__sub0(int id)
{
    TMapSuperItem &sitem = _levelInfo.SuperItems[id];

    for( NC_STACK_ypabact * &unit : _unitsList )
    {
        if ( unit->_bact_type == BACT_TYPES_ROBO && sitem.ActivateOwner == unit->_owner )
        {
            if ( sub_4D11C0(id, sitem.ActivateOwner) )
            {
                if ( sitem.CountDown > 0 )
                    sitem.CountDown = sitem.CountDown - _frameTime;
                else
                    sub_4D1444(id);
            }
            else if ( !sitem.KeySectors.empty() )
            {
                sub_4D1594(id);
            }
            else
            {
                sub_4D12D8(id, 0);
            }
            return;
        }
    }

    sub_4D16C4(id);
}


void ypaworld_func64__sub19__sub3(NC_STACK_ypaworld *yw, int id)
{
    if ( yw->_GameShell )
    {
        TMapSuperItem &sitem = yw->_levelInfo.SuperItems[id];

        int v4 = sitem.CountDown / 1024;

        if ( v4 < 10 && v4 != sitem.LastSec )
        {
            SFXEngine::SFXe.startSound(&yw->_GameShell->samples1_info, 3);
            sitem.LastSec = v4;
        }

        int v5 = v4 / 10;
        if ( v5 != sitem.LastTenSec )
        {
            SFXEngine::SFXe.startSound(&yw->_GameShell->samples1_info, 3);
            sitem.LastTenSec = v5;
        }
    }
}

bool NC_STACK_ypaworld::sub_4D1230(int id, int a3)
{
    const TMapSuperItem &sitem = _levelInfo.SuperItems[id];

    if ( sitem.PCell->owner == a3 )
        return false;

    if ( sitem.KeySectors.empty() )
        return true;

    for ( const TMapKeySector &ks : sitem.KeySectors )
    {
        if (ks.PCell->owner == a3)
            return false;
    }

    return true;
}

void NC_STACK_ypaworld::ypaworld_func64__sub19__sub1(int id)
{
    const TMapSuperItem &sitem = _levelInfo.SuperItems[id];

    for ( NC_STACK_ypabact * &unit : _unitsList )
    {
        if ( unit->_bact_type == BACT_TYPES_ROBO && sitem.ActivateOwner == unit->_owner )
        {
            if ( sub_4D11C0(id, sitem.ActivateOwner) )
                sub_4D12D8(id, 1);
            else if ( sub_4D1230(id, sitem.ActivateOwner) )
                sub_4D16C4(id);
            return;
        }
    }

    sub_4D16C4(id);
}

void NC_STACK_ypaworld::ypaworld_func64__sub19__sub2__sub0__sub0(uint8_t activate, float a5, float a6, float a7)
{
    for(cellArea &cell : _cells)
    {
        for( NC_STACK_ypabact* &bct : cell.unitsList )
        {
            int v9 = 1;

            if ( _isNetGame )
            {
                if ( bct->_owner != _userUnit->_owner || bct->_owner == activate || bct->_status == BACT_STATUS_DEAD )
                    v9 = 0;
            }
            else if ( bct->_owner == activate || bct->_status == BACT_STATUS_DEAD )
            {
                v9 = 0;
            }

            if ( v9 )
            {
                float v10 = a5 - bct->_position.x;
                float v11 = a6 - bct->_position.z;

                if ( sqrt(POW2(v10) + POW2(v11)) < a7 )
                {
                    bact_arg84 arg84;
                    arg84.energy = -22000000;
                    arg84.unit = NULL;

                    bct->ModifyEnergy(&arg84);
                }
            }
        }

    }
}

void NC_STACK_ypaworld::ypaworld_func64__sub19__sub2__sub0(int id)
{
    TMapSuperItem &sitem = _levelInfo.SuperItems[id];

    sitem.CurrentRadius = (_timeStamp - sitem.TriggerTime) * World::CVSectorLength / 2400.0;

    vec2d tmp = World::SectorIDToCenterPos2( sitem.CellId );

    float v19 = sqrt(POW2(_mapLength.x) + POW2(_mapLength.y));

    if ( sitem.CurrentRadius > 300 && sitem.CurrentRadius - sitem.LastRadius > 200 && sitem.CurrentRadius < v19 )
    {
        float v9 = (2 * sitem.CurrentRadius) * C_PI / 150.0;

        sitem.LastRadius = sitem.CurrentRadius;

        if ( v9 > 2.0 )
        {
            for (float v25 = 0.0; v25 < 6.283 ; v25 += 6.283 / v9 )
            {
                float v10 = sitem.CurrentRadius;
                float v26 = cos(v25) * v10 + tmp.x;
                float v21 = sin(v25) * v10 + tmp.y;

                if ( v26 > 600.0 && v21 < -600.0 && v26 < _mapLength.x - 600.0 && v21 > -(_mapLength.y - 600.0) )
                {
                    int v12 = _fxLimit;

                    _fxLimit = 2;

                    yw_arg129 arg129;
                    arg129.pos.x = v26;
                    arg129.pos.y = sitem.PCell->height;
                    arg129.pos.z = v21;
                    arg129.field_10 = 200000;
                    arg129.OwnerID = sitem.ActivateOwner;
                    arg129.unit = 0;

                    ypaworld_func129(&arg129);

                    _fxLimit = v12;
                }
            }
        }
    }

    ypaworld_func64__sub19__sub2__sub0__sub0(sitem.ActivateOwner, tmp.x, tmp.y, sitem.CurrentRadius);
}

void NC_STACK_ypaworld::ypaworld_func64__sub19__sub2(int id)
{
    const TMapSuperItem &sitem = _levelInfo.SuperItems[id];

    if ( !sub_4D1230(id, sitem.ActivateOwner) && sub_4D12A0(sitem.ActivateOwner) )
    {
        if ( sitem.Type == TMapSuperItem::TYPE_BOMB )
            ypaworld_func64__sub19__sub2__sub0(id);
    }
    else
    {
        sub_4D16C4(id);
    }
}

void NC_STACK_ypaworld::ypaworld_func64__sub19()
{
    for (size_t i = 0; i < _levelInfo.SuperItems.size(); i++)
    {
        const TMapSuperItem &sitem = _levelInfo.SuperItems[i];

        if ( sitem.Type != 0 )
        {
            switch ( sitem.State )
            {
            case TMapSuperItem::STATE_INACTIVE:
                if ( sub_4D11C0(i, sitem.PCell->owner) )
                {
                    if ( sub_4D12A0(sitem.PCell->owner) )
                        sub_4D12D8(i, 0);
                }
                break;

            case TMapSuperItem::STATE_ACTIVE:
                ypaworld_func64__sub19__sub0(i);
                ypaworld_func64__sub19__sub3(this, i);
                break;

            case TMapSuperItem::STATE_STOPPED:
                ypaworld_func64__sub19__sub1(i);
                break;

            case TMapSuperItem::STATE_TRIGGED:
                ypaworld_func64__sub19__sub2(i);
                break;

            default:
                break;
            }
        }
    }
}

void NC_STACK_ypaworld::VoiceMessageCalcPositionToUnit()
{
    if ( _voiceMessage.Unit == _userRobo )
    {
        _voiceMessage.Carrier.Position = _userUnit->_position;
    }
    else
    {
        vec3d tmp = _voiceMessage.Unit->_position - _userUnit->_position;

        float v11 = tmp.length();

        if ( v11 > 0.0 )
            tmp *= (100.0 / v11);

        _voiceMessage.Carrier.Position = _userUnit->_position + tmp;
    }
}

void NC_STACK_ypaworld::VoiceMessageUpdate()
{
    if ( IsSpectatorControlled() )
    {
        _voiceMessage.Reset();
        return;
    }

    if ( _voiceMessage.Priority >= 0 )
    {
        if ( _voiceMessage.Unit->_status != BACT_STATUS_DEAD )
        {
            VoiceMessageCalcPositionToUnit();

            _voiceMessage.Carrier.Vector = _userUnit->_fly_dir * _userUnit->_fly_dir_length;
        }

        if ( _voiceMessage.Carrier.Sounds[0].IsEnabled() )
        {
            SFXEngine::SFXe.UpdateSoundCarrier(&_voiceMessage.Carrier);
        }
        else
            _voiceMessage.Reset();
    }
}

void ypaworld_func64__sub3(NC_STACK_ypaworld *yw)
{
    if ( yw->IsSpectatorControlled() )
        return;

    if ( yw->_userUnit->_pSector->owner != yw->_userRobo->_owner )
    {
        if ( yw->_userUnit->_pSector->owner )
        {
            if ( yw->_ownerOldCellUserUnit == yw->_userRobo->_owner || !yw->_ownerOldCellUserUnit )
            {
                if ( yw->_timeStamp - yw->_msgTimestampEnemySector > 10000 )
                {
                    yw_arg159 arg159;
                    arg159.unit = yw->_userUnit;
                    arg159.Priority = 24;
                    arg159.txt = Locale::Text::Feedback(Locale::FEEDBACK_ESECTENTER);
                    arg159.MsgID = 22;

                    yw->ypaworld_func159(&arg159);
                }

                yw->_msgTimestampEnemySector = yw->_timeStamp;
            }
        }
    }
}

void NC_STACK_ypaworld::ProfileCalcValues()
{
    _profileFramesCount++;

    if ( _profileFramesCount >= 5 )
    {
        if ( _profileVals[PFID_FPS] > 200 )
            _profileVals[PFID_FPS] = 0;

        for (size_t i = 0; i < PFID_MAX; i++)
        {
            if ( _profileVals[i] != 0 )
            {
                if ( _profileVals[i] < _profileMin[i] )
                    _profileMin[i] = _profileVals[i];

                if ( _profileVals[i] > _profileMax[i] )
                    _profileMax[i] = _profileVals[i];

                _profileTotal[i] += _profileVals[i];
            }
        }
    }
}

int NC_STACK_ypaworld::yw_RestoreVehicleData()
{
    std::string buf = fmt::sprintf("save:%s/%d.rst", _GameShell->UserName, _levelInfo.LevelID);

    ScriptParser::HandlersList parsers {
        new World::Parsers::VhclProtoParser(this),
        new World::Parsers::WeaponProtoParser(this),
        new World::Parsers::BuildProtoParser(this)
    };

    return ScriptParser::ParseFile(buf, parsers, 0);
}

void NC_STACK_ypaworld::EnableLevelPasses()
{
    if ( _levelInfo.State == TLevelInfo::STATE_COMPLETED )
    {
        TMapGate &gate = _levelInfo.Gates[ _levelInfo.GateCompleteID ];

        _globalMapRegions.MapRegions[ _levelInfo.LevelID ].Status = TMapRegionInfo::STATUS_COMPLETED;

        for (int lvl : gate.PassToLevels)
        {
            if ( _globalMapRegions.MapRegions[ lvl ].Status == TMapRegionInfo::STATUS_DISABLED )
                _globalMapRegions.MapRegions[ lvl ].Status = TMapRegionInfo::STATUS_ENABLED;
        }
    }
    else if ( _levelInfo.State == TLevelInfo::STATE_ABORTED && !yw_RestoreVehicleData() )
    {
        ypa_log_out("yw_RestoreVehicleData() failed.\n");
    }
}

void NC_STACK_ypaworld::NetReleaseMissiles(NC_STACK_ypabact *bact)
{
    while(!bact->_missiles_list.empty())
    {
        NC_STACK_ypamissile *misl = bact->_missiles_list.front();
        bact->_missiles_list.pop_front();

        if ( misl->_primTtype == BACT_TGT_TYPE_UNIT )
        {
            misl->_primT.pbact->DeleteAttacker(misl, 0);
            misl->_primTtype = BACT_TGT_TYPE_NONE;
        }

        misl->CleanAttackersTarget();

        misl->_parent = NULL;

        ypaworld_func144(misl);

        misl->_status_flg |= BACT_STFLAG_DEATH1;
    }
}

void NC_STACK_ypaworld::sub_4F1BE8(NC_STACK_ypabact *bct)
{
    if ( bct->_bact_type == BACT_TYPES_GUN )
    {
        NC_STACK_ypagun *gun = dynamic_cast<NC_STACK_ypagun *>( bct );

        if ( gun->IsRoboGun() )
        {
            for (World::TRoboGun &pgun : bct->_host_station->GetGuns())
            {
                if ( bct == pgun.gun_obj )
                    pgun.gun_obj = NULL;
            }
        }
    }
}

void NC_STACK_ypaworld::NetRemove(NC_STACK_ypabact *bct)
{
    while(!bct->_kidList.empty())
    {
        NC_STACK_ypabact *cmnder = bct->_kidList.front();

        while ( !cmnder->_kidList.empty() )
        {
            NC_STACK_ypabact *slave = cmnder->_kidList.front();

            NetReleaseMissiles(slave);
            slave->CleanAttackersTarget();
            sub_4F1BE8(slave);

            slave->_status_flg |= BACT_STFLAG_DEATH1;
            slave->_status = BACT_STATUS_DEAD;

            ypaworld_func144(slave);
        }

        NetReleaseMissiles(cmnder);
        cmnder->CleanAttackersTarget();
        sub_4F1BE8(cmnder);

        cmnder->_status_flg |= BACT_STFLAG_DEATH1;
        cmnder->_status = BACT_STATUS_DEAD;

        ypaworld_func144(cmnder);
    }

    if ( bct->_bact_type == BACT_TYPES_ROBO )
    {
        NC_STACK_yparobo *robo = dynamic_cast<NC_STACK_yparobo *>(bct);

        for (World::TRoboGun &gun : robo->GetGuns())
            gun.gun_obj = NULL;
    }

    bct->CleanAttackersTarget();
    NetReleaseMissiles(bct);

    bct->_status = BACT_STATUS_DEAD;

    ypaworld_func144(bct);

    bct->_status_flg |= BACT_STFLAG_DEATH1;
}

void NC_STACK_ypaworld::ProtosFreeSounds()
{
    SFXEngine::SFXe.StopPlayingSounds();

    for (World::TVhclProto &vhcl : _vhclProtos)
    {
        for (World::TVhclSound &sfx : vhcl.sndFX)
            sfx.ClearSounds();
    }

    for (World::TWeapProto &wep : _weaponProtos)
    {
        for (World::TVhclSound &fx : wep.sndFXes)
            fx.ClearSounds();

        wep.debuff.tick_snd.ClearSounds();
    }

    for (World::TBuildingProto &proto : _buildProtos)
        proto.SndFX.ClearSounds();
}


void NC_STACK_ypaworld::FFeedback_Update()
{
    if ( _shellConfIsParsed )
    {
        if ( _preferences & (World::PREF_JOYDISABLE | World::PREF_FFDISABLE) )
            return;
    }

    if ( _userUnit )
    {
        if ( _timeStamp - _ffTimeStamp > 250 )
        {
            _ffTimeStamp = _timeStamp;

            if ( _userUnit->_status == BACT_STATUS_DEAD )
            {
                Input::Engine.ForceFeedback(Input::FF_STATE_UPDATE, _ffEffectType);
            }
            else
            {
                if ( _ffEffectType != -1 )
                {
                    float a1 = POW2(_userUnit->_force) - POW2(_userUnit->_mass) * 100.0;

                    if (a1 < 0.0)
                        a1 = 0.0;

                    float v17 = fabs(_userUnit->_fly_dir_length) / ( sqrt(a1) / _userUnit->_airconst_static );
                    if ( v17 >= 1.0 )
                        v17 = 1.0;
                    else if (v17 < 0.0)
                        v17 = 0.0;

                    Input::Engine.ForceFeedback(Input::FF_STATE_UPDATE, _ffEffectType,
                                                        _ffMagnitude, _ffPeriod * v17);
                }
            }
        }

        TSoundSource *top = SFXEngine::SFXe.SndGetTopShake();
        if ( top )
        {
            if ( top->StartTime == SFXEngine::SFXe.currentTime )
            {
                float p1 = top->ShkMag;
                if ( p1 > 1.0 )
                    p1 = 1.0;

                vec3d tmp = top->PCarrier->Position - _userUnit->_position;

                float p2 = top->PShkFx->time;
                float p3 = _userUnit->_rotation.AxisX().dot( tmp );
                float p4 = -_userUnit->_rotation.AxisZ().dot( tmp );

                if ( p2 > 0.0 )
                {
                    Input::Engine.ForceFeedback(Input::FF_STATE_START, Input::FF_TYPE_SHAKE,
                                                        p1, p2, p3, p4);
                }
            }
        }
    }
}

int recorder_startrec(NC_STACK_ypaworld *yw)
{
    TGameRecorder *rcrd = yw->_replayRecorder;

    rcrd->do_record = 0;
    rcrd->field_40 = 0;
    rcrd->seqn++;
    rcrd->level_id = yw->_levelInfo.LevelID;
    rcrd->frame_id = 0;
    rcrd->time = 0;
    rcrd->bacts_count = 0;
    //rcrd->field_34 = 0;
    rcrd->ainf_size = 0;

    rcrd->mfile = IFFile::UAOpenIFFile(fmt::sprintf("env:snaps/m%02d%04d.raw", yw->_levelInfo.LevelID, rcrd->seqn), "wb");

    if ( !rcrd->mfile.OK() )
    {
        return 0;
    }

    rcrd->mfile.pushChunk(TAG_SEQN, TAG_FORM, -1);
    rcrd->mfile.pushChunk(0, TAG_SINF, 4);

    rcrd->mfile.writeU16L(rcrd->seqn);
    rcrd->mfile.writeU16L(rcrd->level_id);

    rcrd->mfile.popChunk();

    rcrd->do_record = 1;
    return 1;
}

void recorder_stoprec(NC_STACK_ypaworld *yw)
{
    TGameRecorder *rcrd = yw->_replayRecorder;
    rcrd->do_record = 0;

    rcrd->mfile.popChunk();

    rcrd->mfile.close();
}

void sb_0x447720(NC_STACK_ypaworld *yw, TInputState *inpt)
{
    if ( inpt->KbdLastHit == Input::KC_NUMMUL && (inpt->ClickInf.flag & TClickBoxInf::FLAG_RM_HOLD || yw->_easyCheatKeys) )
    {
        sub_4476AC(yw);

        yw_arg159 info_msg;
        info_msg.txt = "Screenshot saved.";
        info_msg.unit = NULL;
        info_msg.Priority = 100;
        info_msg.MsgID = 0;

        yw->ypaworld_func159(&info_msg);
    }


    if ( yw->_screenShotSeq )
    {
        if ( inpt->KbdLastHit == Input::KC_NUMDIV && (inpt->ClickInf.flag & TClickBoxInf::FLAG_RM_HOLD || yw->_easyCheatKeys) )
        {
            yw->_screenShotSeq = false;

            yw_arg159 info_msg;
            info_msg.txt = "Screenshotting: stopped.";
            info_msg.unit = NULL;
            info_msg.Priority = 100;
            info_msg.MsgID = 0;

            yw->ypaworld_func159(&info_msg);
        }

        GFX::Engine.SaveScreenshot( fmt::sprintf("env:snaps/s%d_%04d", yw->_screenShotSeqId, yw->_screenShotSeqFrame) );

        yw->_screenShotSeqFrame++;
    }
    else if ( inpt->KbdLastHit == Input::KC_NUMDIV && (inpt->ClickInf.flag & 0x100 || yw->_easyCheatKeys) )
    {
        yw->_screenShotSeqFrame = 0;
        yw->_screenShotSeq = true;
        yw->_screenShotSeqId++;

        yw_arg159 info_msg;
        info_msg.txt = "Screenshotting: started.";
        info_msg.unit = NULL;
        info_msg.Priority = 100;
        info_msg.MsgID = 0;

        yw->ypaworld_func159(&info_msg);
    }

    if ( yw->_replayRecorder->do_record )
    {
        if ( inpt->KbdLastHit == Input::KC_NUMMINUS && (inpt->ClickInf.flag & TClickBoxInf::FLAG_RM_HOLD || yw->_easyCheatKeys) )
        {
            recorder_stoprec(yw);

            yw_arg159 info_msg;
            info_msg.txt = "Replay recordering: stopped.";
            info_msg.unit = NULL;
            info_msg.Priority = 100;
            info_msg.MsgID = 0;

            yw->ypaworld_func159(&info_msg);
        }

    }
    else
    {
        if ( inpt->KbdLastHit == Input::KC_NUMMINUS && (inpt->ClickInf.flag & TClickBoxInf::FLAG_RM_HOLD || yw->_easyCheatKeys) )
        {
            recorder_startrec(yw);

            yw_arg159 info_msg;
            info_msg.txt = "Replay recordering: started.";
            info_msg.unit = NULL;
            info_msg.Priority = 100;
            info_msg.MsgID = 0;

            yw->ypaworld_func159(&info_msg);
        }
    }
}

void recorder_update_time(NC_STACK_ypaworld *yw, int dtime)
{
    yw->_replayRecorder->time += dtime;
    yw->_replayRecorder->field_40 -= dtime;
}


void NC_STACK_ypaworld::recorder_store_bact( TGameRecorder *rcrd, World::MissileList &bct_lst)
{
    for( NC_STACK_ypamissile * &bact : bct_lst )
    {
        if ( bact->_gid >= 0xFFFF || bact == _userRobo )
        {
            if ( rcrd->bacts_count < rcrd->bacts.size() )
            {
                rcrd->bacts[ rcrd->bacts_count ] = bact;
                rcrd->bacts_count++;
            }

            recorder_store_bact(rcrd, bact->_missiles_list);
            recorder_store_bact(rcrd, bact->_kidList);
        }
    }
}

void NC_STACK_ypaworld::recorder_store_bact( TGameRecorder *rcrd, World::RefBactList &bct_lst)
{
    for( NC_STACK_ypabact * &bact : bct_lst )
    {
        if ( bact->_gid >= 0xFFFF || bact == _userRobo )
        {
            if ( rcrd->bacts_count < rcrd->bacts.size() )
            {
                rcrd->bacts[ rcrd->bacts_count ] = bact;
                rcrd->bacts_count++;
            }

            recorder_store_bact(rcrd, bact->_missiles_list);
            recorder_store_bact(rcrd, bact->_kidList);
        }
    }
}


void NC_STACK_ypaworld::recorder_world_to_frame(TGameRecorder *rcrd)
{
    rcrd->bacts_count = 0;
    recorder_store_bact(rcrd, _unitsList);

    std::sort(rcrd->bacts.begin(), rcrd->bacts.begin() + rcrd->bacts_count, TGameRecorder::BactSortCompare);

    for (uint32_t i = 0; i < rcrd->bacts_count; i++)
    {
        NC_STACK_ypabact *bact = rcrd->bacts[i];

        trec_bct *oinf = &rcrd->oinf[i];

        oinf->bact_id = bact->_gid;
        oinf->pos = bact->_position;

        vec3d euler = bact->_rotation.GetEuler();

        oinf->rot_x = dround(euler.x * 127.0 / C_2PI);
        oinf->rot_y = dround(euler.y * 127.0 / C_2PI);
        oinf->rot_z = dround(euler.z * 127.0 / C_2PI);

        NC_STACK_base *a4 = bact->GetVP();

        if ( a4 == bact->_vp_normal )
        {
            oinf->vp_id = 1;
        }
        else if ( a4 == bact->_vp_fire )
        {
            oinf->vp_id = 2;
        }
        else if ( a4 == bact->_vp_wait )
        {
            oinf->vp_id = 3;
        }
        else if ( a4 == bact->_vp_dead )
        {
            oinf->vp_id = 4;
        }
        else if ( a4 == bact->_vp_megadeth )
        {
            oinf->vp_id = 5;
        }
        else if ( a4 == bact->_vp_genesis )
        {
            oinf->vp_id = 6;
        }
        else
        {
            oinf->vp_id = 0;
        }

        if (bact->_bact_type == BACT_TYPES_MISSLE)
            oinf->objType = TGameRecorder::OBJ_TYPE_MISSILE;
        else
            oinf->objType = TGameRecorder::OBJ_TYPE_VEHICLE;

        oinf->vhcl_id = bact->_vehicleID;

        TGameRecorder::TSndState &ssnd = rcrd->sound_status[i];
        ssnd.active = 0;

        for (int j = 0; j < 16; j++)
        {
            if (bact->_soundcarrier.Sounds[j].IsEnabled() ||
                bact->_soundcarrier.Sounds[j].IsPFxEnabled() ||
                bact->_soundcarrier.Sounds[j].IsShkEnabled())
                ssnd.active |= 1 << j;
        }

        ssnd.pitch = bact->_soundcarrier.Sounds[0].Pitch;
    }
}

void recorder_pack_soundstates(TGameRecorder *rcrd)
{
    uint8_t *in = (uint8_t *)rcrd->sound_status.data();
    int in_pos = 0;

    uint8_t *output = (uint8_t *)rcrd->ainf.data();
    int out_pos = 0;

    int max_bytes_count = 4 * rcrd->bacts_count;

    while ( in_pos < max_bytes_count )
    {
        if ( in_pos >= max_bytes_count - 1 || in[in_pos] != in[in_pos + 1] )
        {
            int ctrl_byte_pos = out_pos;
            int cnt_bytes = 0;

            while (cnt_bytes < 0x80)
            {
                if ( in_pos >= max_bytes_count )
                    break;
                else if ( in_pos < max_bytes_count - 2 && in[in_pos] == in[in_pos + 1] && in[in_pos] == in[in_pos + 2] )
                    break;

                output[out_pos] = in[in_pos];
                in_pos++;
                out_pos++;

                cnt_bytes++;
            }

            output[ctrl_byte_pos] = cnt_bytes - 1;
        }
        else
        {
            int cnt_bytes = 0;

            uint8_t smplbyte = in[in_pos];
            while ( in_pos < max_bytes_count )
            {
                if ( in[in_pos] != smplbyte )
                    break;

                if ( cnt_bytes >= 0x80 )
                    break;

                in_pos++;
                cnt_bytes++;
            }
            output[out_pos] = 0x101 - cnt_bytes;
            output[out_pos + 1] = smplbyte;
            out_pos += 2;
        }
    }

    rcrd->ainf_size = out_pos;
}

void recorder_unpack_soundstates(TGameRecorder *rcrd)
{
    uint8_t *out = (uint8_t *)rcrd->sound_status.data();
    uint8_t *in = (uint8_t *)rcrd->ainf.data();
    uint8_t *in_end = ((uint8_t *)rcrd->ainf.data()) + rcrd->ainf_size;

    while ( in < in_end )
    {
        uint8_t bt = *in;
        in++;

        if ( bt > 0x80 )
        {
            for (int i = 0; i < 0x101 - bt; i++)
            {
                *out = *in;
                out++;
            }

            in++;
        }
        else if ( bt < 0x80 )
        {
            bt += 1;

            memcpy(out, in, bt);

            out += bt;
            in += bt;
        }
    }
}

void NC_STACK_ypaworld::recorder_write_frame()
{
    TGameRecorder *rcrd = _replayRecorder;

    if ( rcrd->field_40 < 0 )
    {
        recorder_world_to_frame(rcrd);
        rcrd->ctrl_bact_id = _userUnit->_gid;
        recorder_pack_soundstates(rcrd);


        int frame_size = 24;
        int oinf_size = 22 * rcrd->bacts_count;
        int v5 = 0;//16 * rcrd->field_34;

        if ( oinf_size )
        {
            frame_size = oinf_size + 32;

            if ( frame_size & 1 )
                frame_size++;
        }

        if ( rcrd->ainf_size )
        {
            frame_size += rcrd->ainf_size + 8;

            if ( frame_size & 1 )
                frame_size++;
        }

        if ( v5 )
        {
            frame_size += v5 + 8;

            if ( frame_size & 1 )
                frame_size++;
        }
        rcrd->mfile.pushChunk(TAG_FRAM, TAG_FORM, frame_size);
        rcrd->mfile.pushChunk(0, TAG_FINF, 12);

        rcrd->mfile.writeS32L(rcrd->frame_id);
        rcrd->mfile.writeS32L(rcrd->time);
        rcrd->mfile.writeU32L(rcrd->ctrl_bact_id);

        rcrd->mfile.popChunk();

        if ( oinf_size )
        {
            rcrd->mfile.pushChunk(0, TAG_OINF, oinf_size);

            for (uint32_t i = 0; i < rcrd->bacts_count; i++)
            {
                trec_bct *oinf = &rcrd->oinf[i];

                rcrd->mfile.writeU32L(oinf->bact_id);
                TF::Engine.Vec3dWriteIFF(oinf->pos, &rcrd->mfile, false);
                rcrd->mfile.writeS8(oinf->rot_x);
                rcrd->mfile.writeS8(oinf->rot_y);
                rcrd->mfile.writeS8(oinf->rot_z);
                rcrd->mfile.writeU8(oinf->vp_id);
                rcrd->mfile.writeU8(oinf->objType);
                rcrd->mfile.writeU8(oinf->vhcl_id);
            }

            rcrd->mfile.popChunk();
        }

        if ( rcrd->ainf_size )
        {
            rcrd->mfile.pushChunk(0, TAG_AINF, rcrd->ainf_size);
            rcrd->mfile.write(rcrd->ainf.data(), rcrd->ainf_size);
            rcrd->mfile.popChunk();
        }

        if ( v5 )
        {
            //rcrd->mfile.pushChunk(0, TAG_MODE, v5);
            //rcrd->mfile.write(rcrd->field_20, v5);
            //rcrd->mfile.popChunk();
        }

        rcrd->mfile.popChunk();

        //rcrd->field_34 = 0;
        rcrd->field_40 += 250;
        rcrd->frame_id += 1;
    }
}


int recorder_open_replay(TGameRecorder *rcrd)
{
    rcrd->mfile = IFFile( uaOpenFile(rcrd->filename, "rb") );

    if ( !rcrd->mfile.OK() )
    {
        return 0;
    }

    if ( rcrd->mfile.parse() != IFFile::IFF_ERR_OK )
    {
        rcrd->mfile.close();
        return 0;
    }

    if ( rcrd->mfile.GetCurrentChunk().Is(TAG_FORM, TAG_SEQN) )
        return 1;

    return 0;
}


bool NC_STACK_ypaworld::recorder_create_camera()
{
    NC_STACK_ypabact *bacto = Nucleus::CInit<NC_STACK_ypabact>( {{NC_STACK_ypabact::BACT_ATT_WORLD, this}} );

    if ( !bacto )
        return false;

    bacto->Renew();

    bacto->_gid = 0;
    bacto->_owner = 1;

    bacto->_rotation = mat3x3::Ident();

    bacto->_soundcarrier.Clear();

    ypaworld_func134(bacto);

    bacto->setBACT_viewer(true);
    bacto->setBACT_inputting(true);

    _userRobo = bacto;

    TF::Engine.SetViewPoint(&bacto->_tForm);

    return true;
}



void recorder_read_framedata(TGameRecorder *rcrd)
{
    while ( rcrd->mfile.parse() != IFFile::IFF_ERR_EOC )
    {
        const IFFile::Context &v3 = rcrd->mfile.GetCurrentChunk();

        switch ( v3.TAG )
        {
        case TAG_FLSH:
            rcrd->field_78 |= 1;
            rcrd->mfile.parse();
            break;

        case TAG_FINF:
            rcrd->frame_id = rcrd->mfile.readS32L();
            rcrd->time = rcrd->mfile.readS32L();
            rcrd->ctrl_bact_id = rcrd->mfile.readU32L();
            rcrd->mfile.parse();
            break;

        case TAG_OINF:
        {
            rcrd->bacts_count = v3.TAG_SIZE / 22;

            for (uint32_t i = 0; i < rcrd->bacts_count; i++)
            {
                trec_bct *oinf = &rcrd->oinf[i];

                oinf->bact_id = rcrd->mfile.readU32L();
                TF::Engine.Vec3dReadIFF(&oinf->pos, &rcrd->mfile, false);
                oinf->rot_x = rcrd->mfile.readS8();
                oinf->rot_y = rcrd->mfile.readS8();
                oinf->rot_z = rcrd->mfile.readS8();

                oinf->vp_id = rcrd->mfile.readU8();
                oinf->objType = rcrd->mfile.readU8();
                oinf->vhcl_id = rcrd->mfile.readU8();
            }

            rcrd->mfile.parse();
        }
        break;

        case TAG_AINF:
            rcrd->mfile.read(rcrd->ainf.data(), v3.TAG_SIZE);
            rcrd->ainf_size = v3.TAG_SIZE;

            recorder_unpack_soundstates(rcrd);

            rcrd->mfile.parse();
            break;

        case TAG_MODE:
            //rcrd->mfile.read(rcrd->field_20, v3.TAG_SIZE);
            //rcrd->field_34 = v3.TAG_SIZE / 16;

            //rcrd->mfile.parse();
            rcrd->mfile.skipChunk();
            break;

        default:
            rcrd->mfile.skipChunk();
            break;
        }
    }
}

NC_STACK_ypabact *NC_STACK_ypaworld::recorder_newObject(trec_bct *oinf)
{
    NC_STACK_ypabact *bacto = NULL;

    if ( oinf->objType == TGameRecorder::OBJ_TYPE_VEHICLE )
    {
        if ( oinf->vhcl_id )
        {
            ypaworld_arg146 arg146;
            arg146.vehicle_id = oinf->vhcl_id;
            arg146.pos = vec3d(0.0, 0.0, 0.0);

            World::TVhclProto *prot = &_vhclProtos[ oinf->vhcl_id ];

            int v6 = prot->model_id;

            prot->model_id = BACT_TYPES_BACT;

            bacto = ypaworld_func146(&arg146);

            _vhclProtos[oinf->vhcl_id].model_id = v6;
        }
        else
        {

            bacto = Nucleus::CInit<NC_STACK_ypabact>( {{NC_STACK_ypabact::BACT_ATT_WORLD, this}} );
            if ( bacto )
            {
                bacto->Renew();

                bacto->_gid = 0;
                bacto->_owner = 1;

                bacto->_rotation = mat3x3::Ident();
            }
        }
    }
    else
    {
        ypaworld_arg146 arg147;
        arg147.vehicle_id = oinf->vhcl_id;
        arg147.pos = vec3d(0.0, 0.0, 0.0);

        bacto = ypaworld_func147(&arg147);
    }

    if ( bacto )
    {
        bacto->_kidRef.Detach();

        bacto->_gid = oinf->bact_id;
        bacto->_host_station = (NC_STACK_yparobo *)_userUnit;
        bacto->_parent = _userUnit;
    }

    return bacto;
}

void NC_STACK_ypaworld::recorder_set_bact_pos(NC_STACK_ypabact *bact, const vec3d &pos)
{
    yw_130arg arg130;
    arg130.pos_x = pos.x;
    arg130.pos_z = pos.z;

    if ( GetSectorInfo(&arg130) )
    {
        if ( bact->_pSector )
            bact->_cellRef.Detach();

        bact->_cellRef = arg130.pcell->unitsList.push_back(bact);

        bact->_pSector = arg130.pcell;
        bact->_old_pos = bact->_position;
        bact->_position = pos;
        bact->_cellId = arg130.CellId;
    }
}

void NC_STACK_ypaworld::recorder_updateObject(NC_STACK_ypabact *bact, trec_bct *oinf, TGameRecorder::TSndState *ssnd, float a5, float a6)
{
    vec3d bct_pos;
    bct_pos = (oinf->pos - bact->_position) * a5 + bact->_position;

    recorder_set_bact_pos(bact, bct_pos);

    bact->_fly_dir = bact->_position - bact->_old_pos;

    float ln = bact->_fly_dir.length();
    if ( ln > 0.0 )
    {
        bact->_fly_dir /= ln;

        if ( a6 <= 0.0 )
            bact->_fly_dir_length = 0;
        else
            bact->_fly_dir_length = (ln / a6) / 6.0;
    }
    else
    {
        bact->_fly_dir = vec3d::OX(1.0);

        bact->_fly_dir_length = 0;
    }

    mat3x3 tmp = mat3x3::Euler( vec3d(oinf->rot_x, oinf->rot_y, oinf->rot_z) / 127.0 * C_2PI );

    vec3d axisX = (tmp.AxisX() - bact->_rotation.AxisX()) * a5 + bact->_rotation.AxisX();

    if ( axisX.normalise() == 0.0 )
        axisX = vec3d::OX(1.0);

    vec3d axisY = (tmp.AxisY() - bact->_rotation.AxisY()) * a5 + bact->_rotation.AxisY();

    if ( axisY.normalise() == 0.0 )
        axisY = vec3d::OY(1.0);

    vec3d axisZ = (tmp.AxisZ() - bact->_rotation.AxisZ()) * a5 + bact->_rotation.AxisZ();

    if ( axisZ.normalise() == 0.0 )
        axisZ = vec3d::OZ(1.0);

    bact->_rotation = mat3x3::Basis(axisX, axisY, axisZ);

    switch ( oinf->vp_id )
    {
    case 1:
        bact->SetVP(bact->_vp_normal);
        break;

    case 2:
        bact->SetVP(bact->_vp_fire);
        break;

    case 3:
        bact->SetVP(bact->_vp_wait);
        break;

    case 4:
        bact->SetVP(bact->_vp_dead);
        break;

    case 5:
        bact->SetVP(bact->_vp_megadeth);
        break;

    case 6:
        bact->SetVP(bact->_vp_genesis);
        break;

    default:
        break;
    }

    bact->_soundcarrier.Sounds[0].Pitch = ssnd->pitch;

    for(int i = 0; i < 16; i++)
    {
        int v48 = 1 << i;
        if ( v48 & ssnd->active )
        {
            if ( !(bact->_soundFlags & v48) )
            {
                bact->_soundFlags |= v48;
                SFXEngine::SFXe.startSound(&bact->_soundcarrier, i);
            }
        }
        else
        {
            if ( bact->_soundFlags & v48 )
            {
                bact->_soundFlags &= ~v48;

                if ( bact->_soundcarrier.Sounds[i].IsLoop() )
                    SFXEngine::SFXe.sub_424000(&bact->_soundcarrier, i);
            }
        }
    }
}


void NC_STACK_ypaworld::recorder_updateObjectList(TGameRecorder *rcrd, float a5, int period)
{
    float fperiod = period / 1000.0;
    World::RefBactList::iterator it = _userUnit->_kidList.begin();

    uint32_t i = 0;

    while ( i < rcrd->bacts_count )
    {
        trec_bct *oinf = &rcrd->oinf[i];
        TGameRecorder::TSndState &ssnd = rcrd->sound_status[i];

        if ( it != _userUnit->_kidList.end() )
        {
            NC_STACK_ypabact *bact = *it;

            if ( oinf->bact_id > bact->_gid )
            {
                it++;

                ypaworld_func144(bact);
            }
            else if ( oinf->bact_id < bact->_gid )
            {
                NC_STACK_ypabact *v10 = recorder_newObject(oinf);

                if ( v10 )
                {
                    recorder_updateObject(v10, oinf, &ssnd, 1.0, fperiod);

                    v10->_kidRef = _userUnit->_kidList.insert(it, v10);

                    i++;
                }
            }
            else // ==
            {
                recorder_updateObject(bact, oinf, &ssnd, a5, fperiod);
                it++;

                i++;
            }
        }
        else
        {
            NC_STACK_ypabact *v13 = recorder_newObject(oinf);

            if ( v13 )
            {
                recorder_updateObject(v13, oinf, &ssnd, 1.0, fperiod);

                v13->_kidRef = _userUnit->_kidList.push_back(v13);
                it = v13->_kidRef;
                it++;

                i++;
            }
        }
    }

    while ( it != _userUnit->_kidList.end() )
    {
        NC_STACK_ypabact *bact = *it;
        it++;

        ypaworld_func144(bact);
    }
}

int NC_STACK_ypaworld::recorder_go_to_frame(TGameRecorder *rcrd, int wanted_frame_id)
{
    int frame_id = wanted_frame_id;
    int cur_frame_id = 0;

    if ( frame_id >= 0 )
    {
        if ( frame_id >= rcrd->field_74 )
            frame_id = rcrd->field_74 - 1;
    }
    else
    {
        frame_id = 0;
    }

    rcrd->mfile.close();

    if ( recorder_open_replay(rcrd) )
    {
        while ( rcrd->mfile.parse() != IFFile::IFF_ERR_EOC )
        {
            if ( rcrd->mfile.GetCurrentChunk().Is(TAG_FORM, TAG_FRAM) )
            {
                if ( cur_frame_id == frame_id )
                {
                    recorder_read_framedata(rcrd);

                    _timeStamp = rcrd->time;

                    recorder_updateObjectList(rcrd, 1.0, 0);
                    return 1;
                }

                cur_frame_id++;
                rcrd->mfile.skipChunk();
            }
            else
            {
                rcrd->mfile.skipChunk();
            }
        }
    }
    return 0;
}


void NC_STACK_ypaworld::ypaworld_func163__sub1(TGameRecorder *rcrd, int dTime)
{
    if ( dTime )
    {
        rcrd->field_78 &= 0xFFFFFFFE;

        while ( rcrd->field_74 - 1 != rcrd->frame_id  &&  (int32_t)(dTime + _timeStamp) > rcrd->time )
        {
            if ( rcrd->mfile.parse() != IFFile::IFF_ERR_EOF )
            {
                if ( rcrd->mfile.GetCurrentChunk().Is(TAG_FORM, TAG_FRAM) )
                    recorder_read_framedata(rcrd);
            }
        }


        if ( rcrd->field_74 - 1 == rcrd->frame_id )
        {
            recorder_go_to_frame(rcrd, 0);
        }
        else
        {
            if ( rcrd->field_78 & 1 )
            {
                _timeStamp = rcrd->time;
                recorder_updateObjectList(rcrd, 1.0, dTime);
            }
            else
            {
                float v9 = (float)dTime / (float)(rcrd->time - _timeStamp);

                _timeStamp += dTime;

                recorder_updateObjectList(rcrd, v9, dTime);
            }
        }
    }
}

void ypaworld_func163__sub2__sub1(NC_STACK_ypaworld *yw, float fperiod, TInputState *inpt)
{
    TGameRecorder *rcrd = yw->_replayPlayer;

    float v20 = rcrd->rotation_matrix.m20;
    float v18 = rcrd->rotation_matrix.m22;

    float v13 = inpt->Sliders[0] * 250.0 * fperiod;
    float v14 = -inpt->Sliders[2] * 250.0 * fperiod;
    float v15 = -inpt->Sliders[1] * 150.0 * fperiod;

    float v17 = sqrt( POW2(v20) + POW2(v18) );
    if ( v17 > 0.0 )
    {
        v20 /= v17;
        v18 /= v17;
    }

    rcrd->field_44.z += v15 * v18;
    rcrd->field_44.x += v15 * v20;

    float v21 = rcrd->rotation_matrix.m00;
    float v19 = rcrd->rotation_matrix.m02;

    float v16 = sqrt( POW2(v21) + POW2(v19) );
    if ( v16 > 0.0 )
    {
        v21 /= v16;
        v19 /= v16;
    }

    rcrd->field_44.y += v14;
    rcrd->field_44.z += v19 * v13;
    rcrd->field_44.x += v21 * v13;
}

void ypaworld_func163__sub2__sub0(NC_STACK_ypaworld *yw, float fperiod, TInputState *inpt)
{
    float v3 = inpt->Sliders[10] * 2.5 * fperiod;

    if ( fabs(v3) > 0.001 )
        yw->_replayPlayer->rotation_matrix = mat3x3::RotateY(-v3) * yw->_replayPlayer->rotation_matrix;

    float v5 = inpt->Sliders[11] * 2.5 * fperiod;

    if ( fabs(v5) > 0.001 )
    {
        yw->_replayPlayer->rotation_matrix = mat3x3::RotateX(v5) * yw->_replayPlayer->rotation_matrix;
    }
}

void NC_STACK_ypaworld::CameraPrepareRender(TGameRecorder *rcrd, NC_STACK_ypabact *bact, TInputState *inpt)
{
    extern tehMap robo_map;
    extern squadMan squadron_manager;

    float fperiod = inpt->Period / 1000.0;

    if ( inpt->ClickInf.flag & TClickBoxInf::FLAG_RM_DOWN )
    {

        if ( _mouseGrabbed )
            _mouseGrabbed = false;
        else if ( inpt->ClickInf.selected_btn != &robo_map  &&  inpt->ClickInf.selected_btn != &squadron_manager )
            _mouseGrabbed = true;
    }

    if ( inpt->Buttons.Is(0) )
        rcrd->rotation_matrix = mat3x3::Ident();

    ypaworld_func163__sub2__sub1(this, fperiod, inpt);

    if ( _mouseGrabbed )
        ypaworld_func163__sub2__sub0(this, fperiod, inpt);

    if ( rcrd->field_80 == 16 )
    {
        recorder_set_bact_pos(bact, rcrd->field_44);
        bact->_rotation = rcrd->rotation_matrix;
    }
    else if ( rcrd->field_80 == 18 )
    {
        for ( NC_STACK_ypabact * &unit : _userRobo->GetKidList() )
        {
            if ( rcrd->field_84 == unit->_gid )
            {
                vec3d v35 = unit->_position + unit->_rotation.Transpose().Transform(rcrd->field_44);
                recorder_set_bact_pos(bact, v35);

                bact->_rotation = rcrd->rotation_matrix * unit->_rotation;
                break;
            }
        }
    }
    else if ( rcrd->field_80 == 20 )
    {
        for ( NC_STACK_ypabact * &unit : _userRobo->GetKidList() )
        {
            if ( rcrd->ctrl_bact_id == unit->_gid )
            {
                vec3d a3a = unit->_position + unit->_rotation.Transpose().Transform(rcrd->field_44);
                recorder_set_bact_pos(bact, a3a);

                bact->_rotation = rcrd->rotation_matrix * unit->_rotation;
                break;
            }
        }
    }

    bact->_fly_dir = bact->_old_pos - bact->_position;

    float v39 = bact->_fly_dir.length();
    if ( v39 <= 0.0 )
    {
        bact->_fly_dir = vec3d::OX(1.0);
        bact->_fly_dir_length = 0;
    }
    else
    {
        bact->_fly_dir /= v39;

        if ( fperiod <= 0.0 )
            bact->_fly_dir_length = 0;
        else
            bact->_fly_dir_length = (v39 / fperiod) / 6.0;
    }

    bact->_tForm.Pos = bact->_position;
    bact->_tForm.SclRot = bact->_rotation;
}

void sub_445654(NC_STACK_ypaworld *yw, CmdStream *in, char *buf, const char *fmt, ...)
{
    FontUA::copy_position(in);

    va_list va;
    va_start(va, fmt);

    vsprintf(buf, fmt, va);

    va_end(va);

    FontUA::add_txt(in, yw->_screenSize.x, 1, buf);
}

void NC_STACK_ypaworld::debug_count_units()
{
    for (int i = 0; i < 8; i++)
    {
        _dbgSquadCounter[i] = 0;
        _dbgVehicleCounter[i] = 0;
        _dbgFlakCounter[i] = 0;
        _dbgRoboCounter[i] = 0;
        _dbgWeaponCounter[i] = 0;
    }

    _dbgTotalSquadCount = 0;
    _dbgTotalVehicleCount = 0;
    _dbgTotalFlakCount = 0;
    _dbgTotalWeaponCount = 0;
    _dbgTotalRoboCount = 0;

    for ( NC_STACK_ypabact * &robo : _unitsList )
    {
        _dbgRoboCounter[ robo->_owner ]++;

        if ( robo->_owner )
        {
            for ( NC_STACK_ypabact * &commander : robo->_kidList )
            {
                bool v5 = false;

                if ( commander->_bact_type == BACT_TYPES_GUN )
                {
                    v5 = true;
                    _dbgFlakCounter[ commander->_owner ]++;
                }
                else
                {
                    _dbgSquadCounter[ commander->_owner ]++;
                    _dbgVehicleCounter[ commander->_owner ]++;
                }

                _dbgWeaponCounter[ commander->_owner ] += commander->_missiles_list.size();

                for ( NC_STACK_ypabact * &unit : commander->_kidList )
                {
                    if ( v5 )
                        _dbgFlakCounter[ unit->_owner ]++;
                    else
                        _dbgVehicleCounter[ commander->_owner ]++;


                    _dbgWeaponCounter[ commander->_owner ] += unit->_missiles_list.size();
                }
            }
        }
    }

    for (int i = 0; i < 8; i++)
    {
        _dbgTotalSquadCount  += _dbgSquadCounter[i];
        _dbgTotalVehicleCount += _dbgVehicleCounter[i];
        _dbgTotalFlakCount  += _dbgFlakCounter[i];
        _dbgTotalWeaponCount  += _dbgWeaponCounter[i];
        _dbgTotalRoboCount += _dbgRoboCounter[i];
    }

    if ( _dbgTotalSquadCount > _dbgTotalSquadCountMax )
        _dbgTotalSquadCountMax = _dbgTotalSquadCount;

    if ( _dbgTotalVehicleCount > _dbgTotalVehicleCountMax )
        _dbgTotalVehicleCountMax = _dbgTotalVehicleCount;

    if ( _dbgTotalFlakCount > _dbgTotalFlakCountMax )
        _dbgTotalFlakCountMax = _dbgTotalFlakCount;

    if ( _dbgTotalWeaponCount > _dbgTotalWeaponCountMax )
        _dbgTotalWeaponCountMax = _dbgTotalWeaponCount;

    if ( _dbgTotalRoboCount > _dbgTotalRoboCountMax )
        _dbgTotalRoboCountMax = _dbgTotalRoboCount;
}

void NC_STACK_ypaworld::debug_info_draw(TInputState *inpt)
{
    if ( _showDebugMode != 0 )
    {
        CmdStream dbg_txt;
        dbg_txt.reserve(4096);
        char buf_sprintf[2048];

        FontUA::select_tileset(&dbg_txt, 15);
        FontUA::set_xpos(&dbg_txt, 8);
        FontUA::set_ypos(&dbg_txt, 16);

        int v104 = 0;

        if ( _showDebugMode == 1 )
        {
            debug_count_units();

            if ( !_buildDate.empty() )
            {
                sub_445654(this, &dbg_txt, buf_sprintf, "build id: %s", _buildDate.c_str());

                FontUA::next_line(&dbg_txt);
            }

            int this_time = _timeStamp / 1024;
            int all_time;

            if ( _isNetGame )
                all_time = 0;
            else
                all_time = (_timeStamp + _playersStats[1].ElapsedTime) / 1024;

            sub_445654(
                      this,
                      &dbg_txt,
                      buf_sprintf,
                      "time: (this: %02d:%02d:%02d) (all: %02d:%02d:%02d)",
                      this_time / 60 / 60,
                      this_time / 60 % 60,
                      this_time % 60,
                      all_time / 60 / 60,
                      all_time / 60 % 60,
                      all_time % 60 );

            FontUA::next_line(&dbg_txt);

            sub_445654(this, &dbg_txt, buf_sprintf, "prof all: %d", _profileVals[PFID_FRAMETIME]);
            FontUA::next_line(&dbg_txt);

            sub_445654(this, &dbg_txt, buf_sprintf, "prof fprint: %d", _profileVals[PFID_MARKTIME]);
            FontUA::next_line(&dbg_txt);

            sub_445654(this, &dbg_txt, buf_sprintf, "prof gui: %d", _profileVals[PFID_GUITIME]);
            FontUA::next_line(&dbg_txt);

            sub_445654(this, &dbg_txt, buf_sprintf, "prof ai: %d", _profileVals[PFID_UPDATETIME]);
            FontUA::next_line(&dbg_txt);

            sub_445654(this, &dbg_txt, buf_sprintf, "prof rend: %d", _profileVals[PFID_RENDERTIME]);
            FontUA::next_line(&dbg_txt);

            sub_445654(this, &dbg_txt, buf_sprintf, "prof 2d rend: %d", _profileVals[PFID_NEWGUITIME]);
            FontUA::next_line(&dbg_txt);

            sub_445654(this, &dbg_txt, buf_sprintf, "prof net: %d", _profileVals[PFID_NETTIME]);
            FontUA::next_line(&dbg_txt);

            sub_445654(this, &dbg_txt, buf_sprintf, "sec type/wtype: %d/%d", _userUnit->_pSector->type_id, _userUnit->_pSector->PurposeType);
            FontUA::next_line(&dbg_txt);

            sub_445654(this, &dbg_txt, buf_sprintf, "beam energy: %d", _beamEnergyCapacity);
            FontUA::next_line(&dbg_txt);

            sub_445654(this, &dbg_txt, buf_sprintf, "num sqd: %d,%d", _dbgTotalSquadCount, _dbgTotalSquadCountMax);
            FontUA::next_line(&dbg_txt);

            sub_445654(this, &dbg_txt, buf_sprintf, "num vhcl: %d,%d", _dbgTotalVehicleCount, _dbgTotalVehicleCountMax);
            FontUA::next_line(&dbg_txt);

            sub_445654(this, &dbg_txt, buf_sprintf, "num flk: %d,%d", _dbgTotalFlakCount, _dbgTotalFlakCountMax);
            FontUA::next_line(&dbg_txt);

            sub_445654(this, &dbg_txt, buf_sprintf, "num robo: %d,%d", _dbgTotalRoboCount, _dbgTotalRoboCountMax);
            FontUA::next_line(&dbg_txt);

            sub_445654(this, &dbg_txt, buf_sprintf, "num wpn: %d,%d", _dbgTotalWeaponCount, _dbgTotalWeaponCountMax);
            FontUA::next_line(&dbg_txt);

            sub_445654(this, &dbg_txt, buf_sprintf, "reload const: %d", _userRobo->_reload_const);
            FontUA::next_line(&dbg_txt);

            sub_445654(
                      this,
                      &dbg_txt,
                      buf_sprintf,
                      "num all vhcl: %d,%d,%d,%d,%d,%d,%d,%d",
                      _countUnitsPerOwner[0],
                      _countUnitsPerOwner[1],
                      _countUnitsPerOwner[2],
                      _countUnitsPerOwner[3],
                      _countUnitsPerOwner[4],
                      _countUnitsPerOwner[5],
                      _countUnitsPerOwner[6],
                      _countUnitsPerOwner[7]);
            FontUA::next_line(&dbg_txt);

            sub_445654(
                      this,
                      &dbg_txt,
                      buf_sprintf,
                      "rld ratio: %8.2f,%8.2f,%8.2f,%8.2f,%8.2f,%8.2f,%8.2f,%8.2f",
                      _reloadRatioPositive[0],
                      _reloadRatioPositive[1],
                      _reloadRatioPositive[2],
                      _reloadRatioPositive[3],
                      _reloadRatioPositive[4],
                      _reloadRatioPositive[5],
                      _reloadRatioPositive[6],
                      _reloadRatioPositive[7]);
            FontUA::next_line(&dbg_txt);

            if ( _invulnerable )
                sub_445654(this, &dbg_txt, buf_sprintf, "invulnerable: %s", "YES");
            else
                sub_445654(this, &dbg_txt, buf_sprintf, "invulnerable: %s", "NO");
            FontUA::next_line(&dbg_txt);
        }
        else if ( _showDebugMode == 2 )
        {
            if ( _GameShell && _isNetGame )
            {
                FontUA::next_line(&dbg_txt);
                FontUA::next_line(&dbg_txt);

                for (UserData::TNetPlayerData &pl : _GameShell->netPlayers)
                {
                    if ( pl.Status )
                    {
                        const char *v35;
                        const char *v36;

                        switch ( pl.Owner )
                        {
                        case 1:
                            v35 = "Resistance";
                            break;

                        case 3:
                            v35 = "Mykonier  ";
                            break;

                        case 4:
                            v35 = "Taerkasten";
                            break;

                        case 6:
                            v35 = "Ghorkov   ";
                            break;

                        default:
                            v35 = "Hae?!     ";
                            break;
                        }

                        switch ( pl.Status )
                        {
                        case 1:
                            v36 = "OK";
                            break;

                        case 2:
                            v36 = "makes trouble";
                            break;

                        case 3:
                            v36 = "left the game";
                            break;

                        case 4:
                            v36 = "Removed";
                            break;

                        default:
                            v36 = "Hae?!     ";
                            break;
                        }

                        sub_445654(this, &dbg_txt, buf_sprintf, "%s status: %s latency: %d", v35, v36, pl.Latency);

                        FontUA::next_line(&dbg_txt);
                    }
                }

                FontUA::next_line(&dbg_txt);

                sub_445654(this, &dbg_txt, buf_sprintf, "net send: %d bytes/sec", _GameShell->netsend_speed);
                FontUA::next_line(&dbg_txt);

                sub_445654(this, &dbg_txt, buf_sprintf, "net rcv: %d bytes/sec", _GameShell->netrecv_speed);
                FontUA::next_line(&dbg_txt);

                sub_445654(this, &dbg_txt, buf_sprintf, "packet: %d bytes", _GameShell->net_packet_size);
                FontUA::next_line(&dbg_txt);

                if ( _netInfoOverkill )
                    sub_445654(this, &dbg_txt, buf_sprintf, "WARNING: INFO OVERKILL");

                FontUA::next_line(&dbg_txt);

                if ( _netDriver )
                {
                    int v100[7];
                    _netDriver->GetStats(v100);

                    FontUA::next_line(&dbg_txt);

                    sub_445654(this, &dbg_txt, buf_sprintf, "thread send list now: %d", v100[0]);
                    FontUA::next_line(&dbg_txt);

                    sub_445654(this, &dbg_txt, buf_sprintf, "thread recv list now: %d", v100[1]);
                    FontUA::next_line(&dbg_txt);

                    sub_445654(this, &dbg_txt, buf_sprintf, "thread send list max: %d", v100[3]);
                    FontUA::next_line(&dbg_txt);

                    sub_445654(this, &dbg_txt, buf_sprintf, "thread recv list max: %d", v100[2]);
                    FontUA::next_line(&dbg_txt);

                    sub_445654(this, &dbg_txt, buf_sprintf, "send call now: %d", v100[4]);
                    FontUA::next_line(&dbg_txt);

                    sub_445654(this, &dbg_txt, buf_sprintf, "send call max: %d", v100[5]);
                    FontUA::next_line(&dbg_txt);

                    sub_445654(this, &dbg_txt, buf_sprintf, "send bugs: %d", v100[6]);
                    FontUA::next_line(&dbg_txt);
                }
            }
            else
            {
                sub_445654(this, &dbg_txt, buf_sprintf, "not a network game");
                FontUA::next_line(&dbg_txt);
            }
        }
        else if ( _showDebugMode == 3 )
        {
            for (int i = 0; i < 17; i++)
            {
                sub_445654(this, &dbg_txt, buf_sprintf, "slider[%d] = %f", i, inpt->Sliders[i]);
                FontUA::next_line(&dbg_txt);
            }

            std::string buf;

            for (int i = 0; i < 32; i++)
            {
                if ( inpt->Buttons.Is(i) )
                    buf += 'O';
                else
                    buf += '_';
            }

            sub_445654(this, &dbg_txt, buf_sprintf, buf.c_str());
            FontUA::next_line(&dbg_txt);

            sub_445654(this, &dbg_txt, buf_sprintf, "keycode = %d", inpt->KbdLastDown);
            FontUA::next_line(&dbg_txt);
        }
        else
        {
            int v109 = 0;
            int v110 = 0;

            for ( NC_STACK_ypabact * &bact : _unitsList )
            {
                if (v109)
                    break;

                if ( bact->_bact_type == BACT_TYPES_ROBO && bact->_owner != 1 ) // FIXME owner
                {
                    v110++;

                    if ( _showDebugMode - 3 <= v110 )
                    {
                        NC_STACK_yparobo *robo = dynamic_cast<NC_STACK_yparobo *>(bact);

                        v109 = 1;

                        sub_445654(this, &dbg_txt, buf_sprintf, "robo owner %d with energy %d / %d / %d / %d", robo->_owner, robo->_energy, robo->_roboBuildSpare, robo->_roboVehicleSpare, robo->_energy_max);
                        FontUA::next_line(&dbg_txt);

                        const char *v71;
                        const char *v73;

                        switch (robo->_roboBuildingDuty)
                        {
                        case 0x10:
                            v71 = "radar";
                            break;

                        case 0x20:
                            v71 = "powerstation";
                            break;

                        case 0x100:
                            v71 = "flak";
                            break;

                        case 0x20000:
                            v71 = "location";
                            break;

                        default:
                            v71 = "nothing";
                            break;
                        }

                        switch (robo->_roboVehicleDuty)
                        {
                        case 0:
                            v73 = "nothing";
                            break;

                        case 0x40:
                            v73 = "conquer";
                            break;

                        case 0x80:
                            v73 = "defense";
                            break;

                        case 0x80000:
                            v73 = "recon";
                            break;

                        case 0x200000:
                            v73 = "robo";
                            break;

                        default:
                            v73 = "powerstation";
                            break;
                        }

                        sub_445654(this, &dbg_txt, buf_sprintf, "    do build job   >%s<   and vhcl job   >%s<", v71, v73);
                        FontUA::next_line(&dbg_txt);

                        sub_445654(this, &dbg_txt, buf_sprintf, "    wait power %d, radar %d, flak %d, location %d",
                                         robo->_roboPowerDelay / 1000,
                                         robo->_roboRadarDelay / 1000,
                                         robo->_roboSafetyDelay / 1000,
                                         robo->_roboPositionDelay / 1000);

                        FontUA::next_line(&dbg_txt);

                        sub_445654(this, &dbg_txt, buf_sprintf, "    wait conquer %d, defense %d, recon %d, robo %d",
                                         robo->_roboConqDelay / 1000,
                                         robo->_roboEnemyDelay / 1000,
                                         robo->_roboExploreDelay / 1000,
                                         robo->_roboDangerDelay / 1000);
                        FontUA::next_line(&dbg_txt);

                        sub_445654(this, &dbg_txt, buf_sprintf, "    values  ");
                        FontUA::next_line(&dbg_txt);

                        if ( robo->_roboPowerDelay > 0 )
                            sub_445654(this, &dbg_txt, buf_sprintf, "power -1, ");
                        else
                            sub_445654(this, &dbg_txt, buf_sprintf, "power %d, ", robo->_roboPowerValue);

                        FontUA::next_line(&dbg_txt);

                        if ( robo->_roboRadarDelay > 0 )
                            sub_445654(this, &dbg_txt, buf_sprintf, "radar -1, ");
                        else
                            sub_445654(this, &dbg_txt, buf_sprintf, "radar %d, ", robo->_roboRadarValue);

                        FontUA::next_line(&dbg_txt);

                        if ( robo->_roboSafetyDelay > 0 )
                            sub_445654(this, &dbg_txt, buf_sprintf, "flak -1, ");
                        else
                            sub_445654(this, &dbg_txt, buf_sprintf, "flak %d, ", robo->_roboSafetyValue);

                        FontUA::next_line(&dbg_txt);

                        if ( robo->_roboPositionDelay > 0 )
                            sub_445654(this, &dbg_txt, buf_sprintf, "power -1, ");
                        else
                            sub_445654(this, &dbg_txt, buf_sprintf, "power %d, ", robo->_roboPositionValue);

                        FontUA::next_line(&dbg_txt);

                        if ( robo->_roboEnemyDelay > 0 )
                            sub_445654(this, &dbg_txt, buf_sprintf, "defense -1, ");
                        else
                            sub_445654(this, &dbg_txt, buf_sprintf, "defense %d, ", robo->_roboEnemyValue);

                        FontUA::next_line(&dbg_txt);

                        if ( robo->_roboConqDelay > 0 )
                            sub_445654(this, &dbg_txt, buf_sprintf, "conquer -1, ");
                        else
                            sub_445654(this, &dbg_txt, buf_sprintf, "conquer %d, ", robo->_roboConqValue);

                        FontUA::next_line(&dbg_txt);

                        if ( robo->_roboExploreDelay > 0 )
                            sub_445654(this, &dbg_txt, buf_sprintf, "recon -1, ");
                        else
                            sub_445654(this, &dbg_txt, buf_sprintf, "recon %d, ", robo->_roboExploreValue);

                        FontUA::next_line(&dbg_txt);

                        if ( robo->_roboDangerDelay > 0 )
                            sub_445654(this, &dbg_txt, buf_sprintf, "robo -1, ");
                        else
                            sub_445654(this, &dbg_txt, buf_sprintf, "robo %d, ", robo->_roboDangerValue);

                        FontUA::next_line(&dbg_txt);

                        if ( robo->_roboState & NC_STACK_yparobo::ROBOSTATE_DOCKINUSE )
                            sub_445654(this, &dbg_txt, buf_sprintf, "dock energy %d time %d", robo->_roboDockEnerg, robo->_roboDockTime);
                    }
                }
            }

            if ( !v109 )
                v104 = 1;
        }


        FontUA::next_line(&dbg_txt);

        sub_445654(this, &dbg_txt, buf_sprintf, "fps: %d", _FPS);
        FontUA::next_line(&dbg_txt);

        sub_445654(this, &dbg_txt, buf_sprintf, "polys: %d,%d", _polysCount, _polysDraw);
        FontUA::next_line(&dbg_txt);

        FontUA::set_end(&dbg_txt);

        GFX::Engine.ProcessDrawSeq(dbg_txt);


        if ( v104 )
            _showDebugMode = 0;
    }

    bool openUADebug = System::IniConf::IsGameNewDebugEnabled();
    if ( !openUADebug )
    {
        _showCollDebug = false;
        _hideHudForScreenshots = false;
        _debugGameplayFrozen = false;
    }
    else
    {
        // F10 collision debug overlay: direct key check, no RMB/easy-cheat helper.
        if ( inpt && inpt->KbdLastHit == Input::KC_F10 )
            _showCollDebug = !_showCollDebug;

        // F11 screenshot mode: hide the gameplay HUD without enabling debug overlays.
        if ( inpt && inpt->KbdLastHit == Input::KC_F11 )
            _hideHudForScreenshots = !_hideHudForScreenshots;
    }

    ExpireDebugAoeRings();

    if ( _showCollDebug )
        debug_draw_coll_spheres();

    // OpenUA custom: keep strategic-map mortar markers from lingering after expiry.
    // Manual mortar control is handled entirely by 2D-map clicks (no key trigger);
    // see NC_STACK_ypaworld::HandleMortarMapClick().
    ExpireMortarMarkers();
}

static bool yw_DebugIsLiveBact(NC_STACK_ypabact *unit)
{
    return unit &&
           !unit->_kidRef.IsListType(World::BLIST_CACHE) &&
           unit->_status != BACT_STATUS_NOPE &&
           unit->_status != BACT_STATUS_DEAD &&
           unit->_status != BACT_STATUS_CREATE &&
           unit->_status != BACT_STATUS_BEAM &&
           !(unit->_status_flg & (BACT_STFLAG_DEATH1 | BACT_STFLAG_DEATH2));
}

static void yw_DebugVisitLiveBacts(World::RefBactList &bactList, std::function<void(NC_STACK_ypabact *)> visitor)
{
    std::set<NC_STACK_ypabact *> visited;

    std::function<void(World::MissileList &)> visitMissiles;
    std::function<void(World::RefBactList &)> visitBacts = [&](World::RefBactList &list)
    {
        for (NC_STACK_ypabact *unit : list)
        {
            if ( !yw_DebugIsLiveBact(unit) || !visited.insert(unit).second )
                continue;

            visitor(unit);
            visitBacts(unit->_kidList);
            visitMissiles(unit->_missiles_list);
        }
    };

    visitMissiles = [&](World::MissileList &list)
    {
        for (NC_STACK_ypamissile *missile : list)
        {
            if ( !yw_DebugIsLiveBact(missile) || !visited.insert(missile).second )
                continue;

            visitor(missile);
            visitBacts(missile->_kidList);
            visitMissiles(missile->_missiles_list);
        }
    };

    visitBacts(bactList);
}

static bool yw_DebugRadiusValid(float radius)
{
    return radius > 0.01f && radius < 100000.0f;
}

static bool yw_DebugIsCurrentControlledBact(NC_STACK_ypaworld *world, NC_STACK_ypabact *unit)
{
    return world &&
           unit &&
           (unit == world->_userUnit ||
            (unit == world->_viewerBact && unit->getBACT_inputting()));
}

void NC_STACK_ypaworld::debug_draw_coll_spheres()
{
    SDL_Surface *scr = GFX::Engine.Screen();
    int screenW = GFX::Engine.GetScreenW();
    int screenH = GFX::Engine.GetScreenH();

    TF::TForm3D *view = TF::Engine.GetViewPoint();
    if (!view)
        return;

    const mat4x4f &Proj = GFX::Engine.GetProjectionMatrix();
    float nearZ = GFX::Engine.GetProjectionNear();

    auto project = [&](const vec3d &worldPos, int &sx, int &sy) -> bool {
        vec3d cam = view->CalcSclRot.Transform(worldPos - view->CalcPos);
        if (cam.z < nearZ)
            return false;
        vec3d p = Proj.Transform(cam);
        float w = Proj.CalcW(cam);
        if (w <= 0.001f)
            return false;
        sx = (int)((p.x / w * 0.5f + 0.5f) * screenW);
        sy = (int)((1.0f - (p.y / w * 0.5f + 0.5f)) * screenH);
        return sx >= -64 && sx <= screenW + 64 && sy >= -64 && sy <= screenH + 64;
    };

    const int SEGS = 12;
    auto drawRing = [&](const vec3d &center, float radius, int axis, uint8_t r, uint8_t g, uint8_t b) {
        if (radius < 0.01f)
            return;

        int px0 = 0, py0 = 0;
        bool hasPrev = false;
        for (int i = 0; i <= SEGS; i++)
        {
            float a = 2.0f * M_PI * i / SEGS;
            float ca = cosf(a) * radius;
            float sa = sinf(a) * radius;
            vec3d p;
            if (axis == 0)
                p = center + vec3d(ca, sa, 0.0);
            else if (axis == 1)
                p = center + vec3d(ca, 0.0, sa);
            else
                p = center + vec3d(0.0, ca, sa);

            int sx = 0, sy = 0;
            if (project(p, sx, sy))
            {
                if (hasPrev)
                    GFX::GFXEngine::DrawLine(scr, Common::Line(px0, py0, sx, sy), r, g, b);
                px0 = sx;
                py0 = sy;
                hasPrev = true;
            }
            else
            {
                hasPrev = false;
            }
        }
    };

    // Single horizontal (XZ) ground ring. Used for large gameplay range radii so the
    // screen does not get cluttered with full wire spheres for every effect.
    auto drawFlatRing = [&](const vec3d &center, float radius, uint8_t r, uint8_t g, uint8_t b) {
        if (radius < 0.01f)
            return;

        int px0 = 0, py0 = 0;
        bool hasPrev = false;
        for (int i = 0; i <= SEGS; i++)
        {
            float a = 2.0f * M_PI * i / SEGS;
            vec3d p = center + vec3d(cosf(a) * radius, 0.0, sinf(a) * radius);
            int sx = 0, sy = 0;
            if (project(p, sx, sy))
            {
                if (hasPrev)
                    GFX::GFXEngine::DrawLine(scr, Common::Line(px0, py0, sx, sy), r, g, b);
                px0 = sx;
                py0 = sy;
                hasPrev = true;
            }
            else
            {
                hasPrev = false;
            }
        }
    };

    auto drawBeamTube = [&](const vec3d &start, const vec3d &end, float radius, uint8_t r, uint8_t g, uint8_t b) {
        if (radius < 0.01f)
            return;

        vec3d axis = end - start;
        float len = axis.length();
        if (len < 1.0f)
            return;
        axis /= len;

        vec3d ref = fabs(axis.y) < 0.9f ? vec3d(0.0f, 1.0f, 0.0f) : vec3d(1.0f, 0.0f, 0.0f);
        vec3d side = ref * axis;
        if (side.normalise() < 0.001f)
            return;

        vec3d up = axis * side;
        if (up.normalise() < 0.001f)
            return;

        auto drawOrientedRing = [&](const vec3d &center) {
            int px0 = 0, py0 = 0;
            bool hasPrev = false;
            for (int i = 0; i <= SEGS; i++)
            {
                float a = 2.0f * M_PI * i / SEGS;
                vec3d p = center + side * (cosf(a) * radius) + up * (sinf(a) * radius);
                int sx = 0, sy = 0;
                if (project(p, sx, sy))
                {
                    if (hasPrev)
                        GFX::GFXEngine::DrawLine(scr, Common::Line(px0, py0, sx, sy), r, g, b);
                    px0 = sx;
                    py0 = sy;
                    hasPrev = true;
                }
                else
                {
                    hasPrev = false;
                }
            }
        };

        int sections = (int)(len / 600.0f) + 2;
        if (sections < 2) sections = 2;
        if (sections > 12) sections = 12;

        for (int i = 0; i <= sections; i++)
        {
            float t = (float)i / (float)sections;
            drawOrientedRing(start + axis * (len * t));
        }

        for (int i = 0; i < 4; i++)
        {
            vec3d offset;
            if (i == 0) offset = side * radius;
            else if (i == 1) offset = side * -radius;
            else if (i == 2) offset = up * radius;
            else offset = up * -radius;

            int sx0 = 0, sy0 = 0, sx1 = 0, sy1 = 0;
            if (project(start + offset, sx0, sy0) && project(end + offset, sx1, sy1))
                GFX::GFXEngine::DrawLine(scr, Common::Line(sx0, sy0, sx1, sy1), r, g, b);
        }
    };

    // Optional near-object labels. There is deliberately no top-left fallback.
    CmdStream labels;
    labels.reserve(4096);
    FontUA::select_tileset(&labels, 15);
    int labelCount = 0;
    const int MAX_LABELS = 40;
    auto drawLabel = [&](const vec3d &worldPos, const char *txt, uint8_t r, uint8_t g, uint8_t b) {
        if (labelCount >= MAX_LABELS)
            return;

        int sx = 0, sy = 0;
        if (!project(worldPos, sx, sy))
            return;

        // Keep labels away from the HUD/top-left/top bars. If it cannot be near the object cleanly, skip it.
        if (sx < 80 || sx > screenW - 180 || sy < 80 || sy > screenH - 80)
            return;

        FontUA::set_xpos(&labels, sx + 6);
        FontUA::set_ypos(&labels, sy - 6);
        // op19: add_txt (op18) outputs at x_out_txt/y_out_txt, NOT at x_out/y_out.
        // Without this copy those stay at 0,0 and every label lands top-left over the HUD.
        FontUA::copy_position(&labels);
        FontUA::set_txtColor(&labels, r, g, b);
        FontUA::add_txt(&labels, 220, 1, txt);
        labelCount++;
    };

    std::vector<NC_STACK_ypabact *> objs;
    objs.reserve(512);

    auto addObj = [&](NC_STACK_ypabact *unit) {
        if (!unit)
            return;
        if (unit->_status_flg & (BACT_STFLAG_DEATH1 | BACT_STFLAG_DEATH2 | BACT_STFLAG_CLEAN))
            return;
        // OpenUA invisible: cloaked stealth units are excluded from the F10 collision/
        // radius debug overlay (radius rings + labels) just like any other UI.
        if (unit->IsInvisibleUnrevealed())
            return;
        for (NC_STACK_ypabact *old : objs)
            if (old == unit)
                return;
        objs.push_back(unit);
    };

    // _unitsList contains major/root BACTs. Cell lists contain live sector occupants, including many normal units/projectiles.
    for (NC_STACK_ypabact *unit : _unitsList)
        addObj(unit);

    for (cellArea &cell : _cells)
    {
        for (NC_STACK_ypabact *unit : cell.unitsList.safe_iter())
            addObj(unit);
    }

    vec3d camPos = view->CalcPos;
    const float RING_MAX_DIST = 8000.0f;
    const float LABEL_MAX_DIST = 3000.0f;

    for (NC_STACK_ypabact *unit : objs)
    {
        float dist = (unit->_position - camPos).length();
        if (dist > RING_MAX_DIST)
            continue;

        bool isSelfControlled = (unit == _userUnit || unit == _viewerBact || unit->getBACT_inputting());
        vec3d pos = unit->_position;
        World::rbcolls *colls = unit->getBACT_collNodes();
        bool compoundReplacesDebugRadius = unit->UsesAutoCollisionSpheres() ||
                                           (unit->_bact_type == BACT_TYPES_ROBO && colls);

        // Red broad/fallback radius. Skip only the self radius to avoid cockpit cross lines.
        if (!isSelfControlled && !compoundReplacesDebugRadius)
        {
            float R = unit->_radius;
            if (R > 0.01f)
            {
                drawRing(pos, R, 0, 220, 60, 60);
                drawRing(pos, R, 1, 220, 60, 60);
                drawRing(pos, R, 2, 220, 60, 60);

                if (dist <= LABEL_MAX_DIST)
                {
                    char buf[64];
                    snprintf(buf, sizeof(buf), "radius=%.0f", R);
                    drawLabel(pos + vec3d::OY(R + 15.0f), buf, 220, 60, 60);
                }
            }
        }

        // Compound collision spheres: green for vehicles, blue for weapons.
        // Generated coll slot/radius/offset labels are intentionally omitted;
        // only the manually meaningful vanilla radius keeps a numeric label.
        if (colls)
        {
            mat3x3 rotT = unit->_rotation.Transpose();
            bool isWeapon = unit->_bact_type == BACT_TYPES_MISSLE;
            uint8_t sphereR = 60;
            uint8_t sphereG = isWeapon ? 130 : 220;
            uint8_t sphereB = isWeapon ? 235 : 60;
            for (const World::TRoboColl &cs : colls->roboColls)
            {
                if (!cs.debug_visible || cs.robo_coll_radius < 0.01f)
                    continue;

                vec3d sphWorld = pos + rotT.Transform(cs.coll_pos);
                float debugRadius = cs.robo_coll_radius;
                drawRing(sphWorld, debugRadius, 0, sphereR, sphereG, sphereB);
                drawRing(sphWorld, debugRadius, 1, sphereR, sphereG, sphereB);
                drawRing(sphWorld, debugRadius, 2, sphereR, sphereG, sphereB);
            }
        }

        // --- GAMEPLAY RANGE RADII (single horizontal ring, distinct colors) ---
        // Drawn flat to keep the overlay readable. Each only appears if its value is set.

        // Blue = power_radius (mobile/static power influence). Read from the vehicle proto.
        if ( unit->_bact_type != BACT_TYPES_MISSLE &&
             unit->_bact_type != BACT_TYPES_ROBO &&
             unit->_bact_type != BACT_TYPES_GUN &&
             (size_t)(unit->_mimic_disguise_vehicleID ? unit->_mimic_disguise_vehicleID : unit->_vehicleID) < _vhclProtos.size() )
        {
            uint8_t protoId = unit->_mimic_disguise_vehicleID ? unit->_mimic_disguise_vehicleID : unit->_vehicleID;
            const World::TVhclProto &proto = _vhclProtos[protoId];
            if ( proto.power > 0 && proto.power_radius > 0.01f )
                drawFlatRing(pos, proto.power_radius, 60, 120, 230);
        }

        // Yellow = spawn trigger radius.
        if ( unit->_spawn_trigger_radius > 0.01f )
            drawFlatRing(pos, unit->_spawn_trigger_radius, 230, 220, 60);

        // Magenta = proximity defense trigger radius.
        if ( unit->_proximity_defense_trigger_radius > 0.01f )
            drawFlatRing(pos, unit->_proximity_defense_trigger_radius, 230, 60, 200);

        // Cyan = seek-and-explode trigger radius.
        if ( unit->_seek_and_explode_trigger_radius > 0.01f )
            drawFlatRing(pos, unit->_seek_and_explode_trigger_radius, 60, 220, 220);

        // Red tube = active model=laser beam thickness, using the weapon radius.
        if ( unit->_laser_active &&
             unit->_laser_weapon >= 0 &&
             (size_t)unit->_laser_weapon < _weaponProtos.size() )
        {
            float laserRadius = _weaponProtos[unit->_laser_weapon].radius;
            if ( laserRadius < 1.0f )
                laserRadius = 1.0f;

            if ( !unit->_laser_beams.empty() )
            {
                for (const NC_STACK_ypabact::TLaserBeamRuntime &beam : unit->_laser_beams)
                    drawBeamTube(beam.start, beam.end, laserRadius, 255, 60, 60);
            }
            else
            {
                drawBeamTube(unit->_laser_beam_start, unit->_laser_beam_end, laserRadius, 255, 60, 60);
            }

            if (dist <= LABEL_MAX_DIST)
            {
                char buf[64];
                snprintf(buf, sizeof(buf), "laser radius=%.0f", laserRadius);
                vec3d labelPos = unit->_laser_beams.empty()
                               ? (unit->_laser_beam_start + unit->_laser_beam_end) * 0.5f
                               : (unit->_laser_beams[0].start + unit->_laser_beams[0].end) * 0.5f;
                drawLabel(labelPos, buf, 255, 60, 60);
            }
        }
    }

    // --- TRANSIENT AoE IMPACT RINGS (lifetime is maintained independently of rendering) ---
    for (const DebugAoeRing &ring : _debugAoeRings)
    {
        if ((ring.pos - camPos).length() <= RING_MAX_DIST)
        {
            if ( ring.sphere )
            {
                drawRing(ring.pos, ring.radius, 0, ring.r, ring.g, ring.b);
                drawRing(ring.pos, ring.radius, 1, ring.r, ring.g, ring.b);
                drawRing(ring.pos, ring.radius, 2, ring.r, ring.g, ring.b);
            }
            else
                drawFlatRing(ring.pos, ring.radius, ring.r, ring.g, ring.b);
        }
    }

    FontUA::reset_tileset(&labels, 15);
    FontUA::set_end(&labels);
    GFX::Engine.ProcessDrawSeq(labels);
}

void NC_STACK_ypaworld::ExpireDebugAoeRings()
{
    for (size_t i = 0; i < _debugAoeRings.size(); )
    {
        const DebugAoeRing &ring = _debugAoeRings[i];
        if ( _timeStamp < ring.createdStamp || _timeStamp >= ring.expireStamp )
        {
            _debugAoeRings[i] = _debugAoeRings.back();
            _debugAoeRings.pop_back();
            continue;
        }
        i++;
    }
}

void NC_STACK_ypaworld::DebugAddAoeRing(const vec3d &pos, float radius, uint8_t r, uint8_t g, uint8_t b)
{
    // Only record while the F10 overlay is active, so there is zero cost when off.
    if ( !_showCollDebug || radius < 0.01f )
        return;

    DebugAoeRing ring;
    ring.pos = pos;
    ring.radius = radius;
    ring.r = r;
    ring.g = g;
    ring.b = b;
    ring.createdStamp = _timeStamp;
    ring.expireStamp = _timeStamp + 1536; // ~1.5s (1024 ticks = 1s)

    _debugAoeRings.push_back(ring);
    if ( _debugAoeRings.size() > 256 )
        _debugAoeRings.erase(_debugAoeRings.begin());
}

void NC_STACK_ypaworld::DebugAddSphere(const vec3d &pos, float radius, uint8_t r, uint8_t g, uint8_t b, int durationMs)
{
    if ( !_showCollDebug || radius < 0.01f || durationMs <= 0 )
        return;

    DebugAoeRing sphere;
    sphere.pos = pos;
    sphere.radius = radius;
    sphere.r = r;
    sphere.g = g;
    sphere.b = b;
    sphere.sphere = true;
    sphere.createdStamp = _timeStamp;
    sphere.expireStamp = _timeStamp + (int32_t)(((int64_t)durationMs * 1024 + 999) / 1000);

    _debugAoeRings.push_back(sphere);
    if ( _debugAoeRings.size() > 256 )
        _debugAoeRings.erase(_debugAoeRings.begin());
}

// OpenUA custom: register/refresh a mortar bombardment marker. Multiple shells of
// the same barrage merge into a single steady ring (refreshing its expiry).
void NC_STACK_ypaworld::AddMortarMarker(const vec3d &pos, float radius, int owner, int lingerMs)
{
    if ( radius < 0.01f )
        return;

    ExpireMortarMarkers();

    if ( lingerMs < 0 )
        lingerMs = 0;

    int32_t expire = _timeStamp + (int32_t)((int64_t)lingerMs * 1024 / 1000); // 1024 ticks = 1s

    for (MortarMarker &m : _mortarMarkers)
    {
        if ( m.owner == (uint8_t)owner && (m.pos.XZ() - pos.XZ()).length() <= radius )
        {
            m.pos = pos;
            m.radius = radius;
            if ( expire > m.expireStamp )
                m.expireStamp = expire;
            return;
        }
    }

    MortarMarker marker;
    marker.pos = pos;
    marker.radius = radius;
    marker.owner = (uint8_t)owner;
    marker.expireStamp = expire;
    _mortarMarkers.push_back(marker);

    if ( _mortarMarkers.size() > 64 )
        _mortarMarkers.erase(_mortarMarkers.begin());
}

// OpenUA custom: expire old bombardment markers without drawing them in the 3D world.
void NC_STACK_ypaworld::ExpireMortarMarkers()
{
    for (size_t i = 0; i < _mortarMarkers.size(); )
    {
        if ( _timeStamp >= _mortarMarkers[i].expireStamp )
        {
            _mortarMarkers[i] = _mortarMarkers.back();
            _mortarMarkers.pop_back();
        }
        else
            i++;
    }
}

void NC_STACK_ypaworld::ClearMortarMarkers()
{
    _mortarMarkers.clear();
}

void NC_STACK_ypaworld::HistoryAktCreate(NC_STACK_ypabact *bact)
{
    HistoryEventAdd( World::History::VhclCreate(bact->_owner, bact->_vehicleID, bact->_position.x * 256.0 / bact->_wrldSize.x, bact->_position.z * 256.0 / bact->_wrldSize.y) );
}

void NC_STACK_ypaworld::HistoryAktKill(NC_STACK_ypabact *bact)
{
    if ( bact->_killer )
    {
        uint8_t owners = (bact->_killer->_owner << 3) | bact->_owner;

        if ( bact->_killer->getBACT_viewer() || (bact->_killer->_status_flg & BACT_STFLAG_ISVIEW) )
            owners |= 0x80;

        if ( bact->getBACT_viewer() || (bact->_status_flg & BACT_STFLAG_ISVIEW) )
            owners |= 0x40;

        uint16_t vp = bact->_vehicleID;

        if ( bact->_bact_type == BACT_TYPES_ROBO )
            vp |= 0x8000;

        HistoryEventAdd( World::History::VhclKill(owners, vp, bact->_position.x * 256.0 / bact->_wrldSize.x, bact->_position.z * 256.0 / bact->_wrldSize.y) );
    }
}
