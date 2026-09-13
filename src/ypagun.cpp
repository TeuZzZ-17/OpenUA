#include <inttypes.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include <math.h>
#include "yw.h"
#include "ypagun.h"
#include "yparobo.h"
#include "world/gunrotation.h"

// OpenNeoUA custom: artillery shell guns are artillery pieces. They must be aimed only by
// UpdateArtilleryShell()/ypabact_AimArtilleryShellLauncherVisual() at the current barrage zone,
// not by the vanilla gun AI that tracks nearby visible enemies.
static bool ypagun_UsesArtilleryShellWeapon(NC_STACK_ypagun *gun)
{
    if ( !gun || !gun->getBACT_pWorld() )
        return false;

    std::vector<World::TWeapProto> &weapons = gun->getBACT_pWorld()->GetWeaponsProtos();
    int candidates[4] = { gun->_weapon, gun->_extra_weapons[0], gun->_extra_weapons[1], gun->_extra_weapons[2] };

    for (int i = 0; i < 4; i++)
    {
        int id = candidates[i];
        if ( id > 0 && (size_t)id < weapons.size() && weapons.at(id).IsArtilleryShell() )
            return true;
    }

    return false;
}

static void ypagun_ClearVanillaArtilleryShellTrackingTarget(NC_STACK_ypagun *gun)
{
    if ( !gun || gun->_secndTtype == BACT_TGT_TYPE_NONE )
        return;

    setTarget_msg clearTarget;
    clearTarget.tgt_type = BACT_TGT_TYPE_NONE;
    clearTarget.priority = 1;
    gun->SetTarget(&clearTarget);
}

size_t NC_STACK_ypagun::Init(IDVList &stak)
{
    if ( !NC_STACK_ypabact::Init(stak) )
        return 0;

    _bact_type = BACT_TYPES_GUN;

    _gunType = GUN_TYPE_REAL;
    _gunFireTime = 100;

    for( auto& it : stak )
    {
        IDVPair &val = it.second;

        if ( !val.Skip )
        {
            switch (val.ID)
            {
            case GUN_ATT_SIDEANGLE:
                setGUN_sideAngle(val.Get<int32_t>());
                break;

            case GUN_ATT_UPANGLE:
                setGUN_upAngle(val.Get<int32_t>());
                break;

            case GUN_ATT_DOWNANGLE:
                setGUN_downAngle(val.Get<int32_t>());
                break;

            case GUN_ATT_FIRETYPE:
                setGUN_fireType(val.Get<int32_t>());
                break;

            case GUN_ATT_FIRETIME:
                setGUN_fireTime(val.Get<int32_t>());
                break;

            case GUN_ATT_SETGROUND:
                setGUN_setGround ( val.Get<int32_t>() );
                break;

            case GUN_ATT_ROBOGUN:
                setGUN_roboGun ( val.Get<int32_t>() );
                break;

            case GUN_ATT_NO_FALL:
                setGUN_doesNotFall( val.Get<int32_t>() );
                break;

            default:
                break;
            }
        }
    }

    return 1;
}

size_t NC_STACK_ypagun::Deinit()
{
    return NC_STACK_ypabact::Deinit();
}

size_t NC_STACK_ypagun::SetParameters(IDVList &stak)
{
    NC_STACK_ypabact::SetParameters(stak);

    for( auto& it : stak )
    {
        IDVPair &val = it.second;

        if ( !val.Skip )
        {
            switch (val.ID)
            {
            case GUN_ATT_SIDEANGLE:
                setGUN_sideAngle(val.Get<int32_t>());
                break;

            case GUN_ATT_UPANGLE:
                setGUN_upAngle(val.Get<int32_t>());
                break;

            case GUN_ATT_DOWNANGLE:
                setGUN_downAngle(val.Get<int32_t>());
                break;

            case GUN_ATT_FIRETYPE:
                setGUN_fireType(val.Get<int32_t>());
                break;

            case GUN_ATT_FIRETIME:
                setGUN_fireTime(val.Get<int32_t>());
                break;

            case GUN_ATT_SETGROUND:
                setGUN_setGround ( val.Get<int32_t>() );
                break;

            case GUN_ATT_ROBOGUN:
                setGUN_roboGun ( val.Get<int32_t>() );
                break;

            case GUN_ATT_NO_FALL:
                setGUN_doesNotFall( val.Get<int32_t>() );
                break;

            default:
                break;
            }
        }
    }

    return 1;
}

bool NC_STACK_ypagun::CheckPedestal()
{
    ypaworld_arg136 arg136;
    arg136.stPos = _position;
    arg136.vect.x = 0;

    if ( _gunFlags & GUN_FLAGS_FALLDOWN )
        arg136.vect.y = -_height;
    else
        arg136.vect.y = _height;

    arg136.vect.z = 0;
    arg136.flags = 0;

    _world->ypaworld_func136(&arg136);

    return arg136.isect != 0;
}

void NC_STACK_ypagun::AI_layer3(update_msg *arg)
{
    if ( IsPassiveModule() )
    {
        // OpenNeoUA: passive modules are fully inert. Only advance the create
        // (genesis scale-in) and death timelines; never aim, fire, acquire
        // targets, or run the pedestal check (that would kill an airborne
        // attachment). Position/orientation are driven by the parent each frame.
        switch ( _status )
        {
        case BACT_STATUS_DEAD:
            DeadTimeUpdate(arg);
            break;

        case BACT_STATUS_CREATE:
            _scale_time -= arg->frameTime;

            if ( _scale_time <= 0 )
            {
                setState_msg arg78;
                arg78.newStatus = BACT_STATUS_NORMAL;
                arg78.setFlags = 0;
                arg78.unsetFlags = 0;

                SetState(&arg78);
            }
            break;

        default:
            break;
        }
        return;
    }

    float fTime = arg->frameTime / 1000.0;

    switch ( _status )
    {
    case BACT_STATUS_NORMAL:
    case BACT_STATUS_IDLE:
    {
        const bool stunned = IsActiveDebuffStunning();
        if ( stunned )
            UpdateActiveDebuffStunMoveIntent();

        if ( !(_gunFlags & (GUN_FLAGS_ROBO | GUN_FLAGS_NO_FALL)) )
        {
            if ( _clock - _gunDownTime > 800 )
            {
                _gunDownTime = _clock;

                if ( !CheckPedestal() )
                {
                    if ( !_world || !_world->IsDebugGlobalInvulnerabilityEnabled() )
                        _energy = -10;
                    break;
                }
            }
        }

        if ( _gunFireCount > 0 )
        {
            _gunFireCount -= arg->frameTime;

            if ( _gunFireCount <= 0 )
            {
                _gunFireCount = 0;

                setState_msg arg78;
                arg78.unsetFlags = BACT_STFLAG_FIRE;
                arg78.newStatus = BACT_STATUS_NOPE;
                arg78.setFlags = 0;

                SetState(&arg78);
            }
        }

        // OpenNeoUA custom: if this gun uses an artillery shell weapon, do not let the
        // vanilla flak/gun AI visually track or fire at nearby enemies. Artillery-shell
        // aiming is handled exclusively by UpdateArtilleryShell() while a barrage is
        // active/pending, so idle artillery shell turrets keep their artillery posture
        // instead of "following" units they cannot directly shoot.
        if ( ypagun_UsesArtilleryShellWeapon(this) && !stunned )
        {
            ypagun_ClearVanillaArtilleryShellTrackingTarget(this);
            break;
        }

        if ( !stunned && _secndTtype != BACT_TGT_TYPE_UNIT )
        {
            if ( !_secndTtype && _gunType == GUN_TYPE_PROTO && _gunFireCount <= 0 )
            {
                if ( _status_flg & BACT_STFLAG_FIRE )
                {
                    setState_msg arg78;
                    arg78.unsetFlags = BACT_STFLAG_FIRE;
                    arg78.newStatus = BACT_STATUS_NOPE;
                    arg78.setFlags = 0;

                    SetState(&arg78);
                }
            }
            break;
        }

        vec3d vTgt;
        if ( stunned )
            vTgt = _target_dir;
        else
            vTgt = _secndT.pbact->_position - GetBodyPosition();

        float dist = vTgt.length();

        if ( dist <= 0.001 )
            break;

        vTgt /= dist;

        //vec3d norm = ypagun.basis * ypagun.rott;

        float xzAngle = 0.0; // Current
        float xzWanted = 0.0; // Wanted

        // local X coordinate axis. Used for calculate rotation on XZ-plane
        // because always lie on XZ-plane
        vec3d lx = _gunRott * _gunBasis;

        // Calculate projection gun rotation X-axis onto XZ basis plane
        vec2d xzRot( _rotation.AxisX().dot(lx),
                     _rotation.AxisX().dot(_gunBasis));

        // Normolise for unit circle.
        // If it's too short - X-rotation-axis near -/+rott axis,
        if (xzRot.normalise() > 0.001)
            xzAngle = xzRot.xyAngle();

        vec2d xzWant( vTgt.dot(_gunBasis),
                      vTgt.dot(-lx) );

        if ( xzWant.normalise() > 0.001 )
            xzWanted = xzWant.xyAngle();
        else
            xzWanted = xzAngle;

        if ( _gunMaxSide <= 3.1 )
        {
            if ( xzWanted < -_gunMaxSide )
                xzWanted = -_gunMaxSide;

            if ( xzWanted > _gunMaxSide )
                xzWanted = _gunMaxSide;
        }

        float xz_delta = xzWanted - xzAngle;

        if ( _gunMaxSide > 3.1 )
        {
            if ( fabs(xz_delta) > C_PI )
            {
                if ( xz_delta < -C_PI )
                    xz_delta += C_2PI;

                if ( xz_delta > C_PI )
                    xz_delta -= C_2PI;
            }
        }

        float MaxRot = _maxrot * fTime;

        if ( xz_delta >= 0.0 )
        {
            if ( xz_delta > MaxRot )
                xz_delta = MaxRot;
        }
        else
        {
            if ( xz_delta < -MaxRot )
                xz_delta = -MaxRot;
        }

        if ( fabs(xz_delta) > 0.001 )
            _rotation *= mat3x3(_gunRott, xz_delta);

        vec3d invRed = -_gunRott;

        float y_angle = clp_asin( invRed.dot(_rotation.AxisZ()) );

        float y_want = clp_asin( invRed.dot( vTgt ) );

        if ( y_want > _gunMaxUp )
            y_want = _gunMaxUp;

        if ( y_want < -_gunMaxDown )
            y_want = -_gunMaxDown;

        float y_delta = y_want - y_angle;

        if ( y_delta > MaxRot )
            y_delta = MaxRot;
        else if ( y_delta < -MaxRot )
            y_delta = -MaxRot;

        if ( fabs(y_delta) > 0.001 )
            _rotation = mat3x3::RotateX(y_delta * 0.3) * _rotation;

        if ( stunned )
            break;

        bact_arg75 arg75;
        arg75.fperiod = fTime;
        arg75.g_time = _clock;
        arg75.target.pbact = _secndT.pbact;

        FightWithBact(&arg75);

        if ( _secndTtype != BACT_TGT_TYPE_UNIT || !_secndT.pbact )
            break;

        if ( !TestTargetSector(_secndT.pbact) )
        {
            setTarget_msg arg67;
            arg67.tgt_type = BACT_TGT_TYPE_NONE;
            arg67.priority = 1;

            SetTarget(&arg67);

            if ( _gunType == GUN_TYPE_PROTO )
            {
                setState_msg arg78;
                arg78.unsetFlags = BACT_STFLAG_FIRE;
                arg78.newStatus = BACT_STATUS_NOPE;
                arg78.setFlags = 0;

                SetState(&arg78);
            }
        }
    }
    break;

    case BACT_STATUS_DEAD:
        DeadTimeUpdate(arg);
        break;

    case BACT_STATUS_CREATE:
        _scale_time -= arg->frameTime;

        if ( _scale_time <= 0 )
        {
            setState_msg arg78;
            arg78.newStatus = BACT_STATUS_NORMAL;
            arg78.setFlags = 0;
            arg78.unsetFlags = 0;

            SetState(&arg78);
        }
        break;

    default:
        break;
    }
}

void NC_STACK_ypagun::User_layer(update_msg *arg)
{
    if ( IsPassiveModule() )
    {
        // OpenNeoUA: passive modules are never player-controlled; stay inert.
        if ( _status == BACT_STATUS_DEAD )
            DeadTimeUpdate(arg);
        return;
    }

    float fTime = arg->frameTime / 1000.0;

    if ( _status == BACT_STATUS_NORMAL )
    {
        if ( _gunFlags & (GUN_FLAGS_ROBO | GUN_FLAGS_NO_FALL) || CheckPedestal() )
        {
            if ( _viewer_position.length() >= 3.0 )
            {
                float v33 = arg->frameTime + 50.0;

                _viewer_position *= 50.0 / v33;
            }
            else
            {
                _viewer_position = 0;
            }

            if ( _status_flg & BACT_STFLAG_FIRE )
            {
                if ( !arg->inpt->Buttons.IsAny({0, 5}) )
                {
                    setState_msg arg78;
                    arg78.setFlags = 0;
                    arg78.newStatus = BACT_STATUS_NOPE;
                    arg78.unsetFlags = BACT_STFLAG_FIRE;

                    SetState(&arg78);
                }
            }

            bact_arg79 arg79;
            arg79.tgType = BACT_TGT_TYPE_CELL;
            arg79.tgt_pos = _position + _rotation.AxisZ() * (World::CVSectorLength * 3.0);

            bact_arg106 arg106;
            arg106.field_4 = _rotation.AxisZ();
            arg106.field_0 = 5;

            if ( UserTargeting(&arg106) )
            {
                arg79.target.pbact = arg106.ret_bact;
                arg79.tgType = BACT_TGT_TYPE_UNIT;
            }

            if ( arg->inpt->Buttons.IsAny({0, 5}) )
            {
                if ( _gunType == GUN_TYPE_REAL )
                {
                    arg79.weapon = _weapon;
                    arg79.direction = _rotation.AxisZ();
                    arg79.g_time = _clock;
                    arg79.start_point = _fire_pos;
                    arg79.flags = (arg->inpt->Buttons.Is(5) ? 1 : 0) | 2;
                    if ( (_oflags & BACT_OFLAG_VIEWER) && arg->inpt->HandBrakePressed )
                        arg79.flags |= BACT_ARG79_FLAG_RECOIL_BRAKE_HELD;

                    if ( LaunchMissile(&arg79) )
                    {
                        _viewer_position = vec3d(0, 0, -25.0);
                    }
                }
                else if ( _gunType == GUN_TYPE_PROTO )
                {
//            if ( ~(ypabact.field_3D6 & 0x100) ) CHECK IT
                    if ( !UsesVehicleMinigunTiming() && !(_status_flg & BACT_STFLAG_FIRE) )
                    {
                        setState_msg arg78;
                        arg78.unsetFlags = 0;
                        arg78.newStatus = BACT_STATUS_NOPE;
                        arg78.setFlags = BACT_STFLAG_FIRE;

                        SetState(&arg78);
                    }

                    bact_arg105 arg105;
                    arg105.field_0 = _rotation.AxisZ();
                    arg105.field_C = fTime;
                    arg105.field_10 = _clock;

                    FireMinigun(&arg105);

                    if ( _gunFlags & GUN_FLAGS_SHOT )
                    {
                        _gunFlags &= ~GUN_FLAGS_SHOT;
                    }
                    else
                    {
                        _gunFlags |= GUN_FLAGS_SHOT;

                        _viewer_position = vec3d(0, -20.0, -30.0);
                    }
                }
            }

            float yRot = arg->inpt->Sliders[0] * _maxrot * fTime;

            if ( fabs( yRot ) > 0.001 )
            {
                vec3d tmp = _gunRott * _gunBasis;

                float yAngle = clp_acos( tmp.dot( _rotation.AxisX() ) );

                if ( tmp.x * _rotation.m02 - tmp.z * _rotation.m00 > 0.0 )
                    yAngle = -yAngle;

                if ( yAngle + yRot < -_gunMaxSide )
                    yRot = -_gunMaxSide - yAngle;

                if ( yAngle + yRot > _gunMaxSide )
                    yRot = _gunMaxSide - yAngle;

                _rotation *= mat3x3(_gunRott, yRot, MAT_FLAG_INV_SIN);
            }

            float xRot = arg->inpt->Sliders[1] * _maxrot * fTime;

            if ( fabs(xRot) > 0.001 )
            {
                vec3d tmp = -_gunRott;
                float xAngle = clp_asin( tmp.dot( _rotation.AxisZ() ) );

                if ( xAngle + xRot < -_gunMaxDown )
                    xRot = -_gunMaxDown - xAngle;

                if ( xAngle + xRot > _gunMaxUp )
                    xRot = _gunMaxUp - xAngle;

                _rotation = mat3x3::RotateX(xRot) * _rotation;
            }

            _viewer_rotation = _rotation;
        }
        else
        {
            if ( !_world || !_world->IsDebugGlobalInvulnerabilityEnabled() )
                _energy = -10;
        }
    }
    else if ( _status == BACT_STATUS_DEAD )
        DeadTimeUpdate(arg);
}

void NC_STACK_ypagun::FightWithBact(bact_arg75 *arg)
{
    // OpenNeoUA custom: artillery shell guns fire only through the artillery shell barrage system.
    // The vanilla close-range gun combat path would make them snap/chase nearby
    // enemies and can launch direct shots, which looks wrong for artillery.
    if ( ypagun_UsesArtilleryShellWeapon(this) )
        return;

    vec3d vTgt = arg->target.pbact->_position - GetBodyPosition();

    float dist = vTgt.length();

    if ( dist < 0.001 )
        return;

    vTgt /= dist;

    // OpenNeoUA: projectile flak guns keep the original 2400 engagement envelope.
    // gun_type = mg (GUN_TYPE_PROTO) instead uses the exact same effective
    // global MGUN range as FireMinigun(), so AI never opens fire beyond the
    // distance its hitscan can actually reach. Missing/invalid config resolves
    // through the existing MGUN fallback (1000).
    const float engagementRange = (_gunType == GUN_TYPE_PROTO) ? GetMinigunRange() : 2400.0f;
    bool arcGrenadeReachable = true;
    bool usesArcGrenade = false;

    if ( _gunType == GUN_TYPE_REAL && _world && _weapon >= 0 &&
         (size_t)_weapon < _world->GetWeaponsProtos().size() )
    {
        const World::TWeapProto &wproto = _world->GetWeaponsProtos().at(_weapon);
        usesArcGrenade = wproto.IsArcGrenade();
        if ( usesArcGrenade )
        {
            vec3d launchDirection;
            const vec3d launchPos =
                GetBodyPosition() + _rotation.Transpose().Transform(_fire_pos);
            arcGrenadeReachable = ypabact_TrySolveArcGrenadeDirection(
                launchPos, arg->target.pbact->_position, wproto, &launchDirection);
        }
    }

    if ( dist <= engagementRange && arcGrenadeReachable &&
         vTgt.dot( _rotation.AxisZ() ) >= 0.95 )
    {
        if ( _gunType == GUN_TYPE_REAL )
        {
            bact_arg79 arg79;
            arg79.direction = vTgt;
            arg79.start_point = _fire_pos;
            arg79.tgType = BACT_TGT_TYPE_UNIT;
            arg79.target.pbact = arg->target.pbact;
            arg79.tgt_pos = arg->target.pbact->_position;
            arg79.weapon = _weapon;
            arg79.g_time = arg->g_time;
            arg79.flags = (!getBACT_inputting() ? 4 : 0) |
                          (usesArcGrenade ? BACT_ARG79_FLAG_AI_BALLISTIC_AIM : 0);

            if ( LaunchMissile(&arg79) )
            {
                _gunFireCount = _gunFireTime;

                setState_msg arg78;
                arg78.unsetFlags = 0;
                arg78.newStatus = BACT_STATUS_NOPE;
                arg78.setFlags = BACT_STFLAG_FIRE;

                SetState(&arg78);
            }
        }
        else if ( _gunType == GUN_TYPE_PROTO )
        {
            if ( !UsesVehicleMinigunTiming() && !(_status_flg & BACT_STFLAG_FIRE) )
            {
                setState_msg arg78;
                arg78.unsetFlags = 0;
                arg78.newStatus = BACT_STATUS_NOPE;
                arg78.setFlags = BACT_STFLAG_FIRE;

                SetState(&arg78);
            }

            bact_arg105 arg105;
            arg105.field_C = arg->fperiod;
            arg105.field_10 = arg->g_time;
            arg105.field_0 = _rotation.AxisZ();

            FireMinigun(&arg105);
        }
    }
    else if ( _gunType == GUN_TYPE_PROTO )
    {
        if ( !UsesVehicleMinigunTiming() )
        {
            setState_msg arg78;
            arg78.setFlags = 0;
            arg78.newStatus = BACT_STATUS_NOPE;
            arg78.unsetFlags = BACT_STFLAG_FIRE;

            SetState(&arg78);
        }
    }
}

void NC_STACK_ypagun::Die()
{
    if ( !(_status_flg & BACT_STFLAG_DEATH1) )
    {
        NC_STACK_ypabact::Die();

        int v6 = 1;
        HandleVisChildrens(&v6);

        if ( (_gunFlags & GUN_FLAGS_ROBO) && _host_station )
        {
            for (World::TRoboGun &gun : _host_station->GetGuns())
            {
                if ( this == gun.gun_obj )
                    gun.gun_obj = NULL;
            }
        }

        if ( (_gunFlags & GUN_FLAGS_ROBO) && _parent )
            _parent->ClearUnitGunPointer(this);

    }
}

size_t NC_STACK_ypagun::SetPosition(bact_arg80 *arg)
{
    if ( !NC_STACK_ypabact::SetPosition(arg) )
        return 0;

    if ( _gunFlags & GUN_FLAGS_GROUND )
    {
        ypaworld_arg136 arg136;
        arg136.stPos.x = arg->pos.x;
        arg136.stPos.y = arg->pos.y + -10000.0;
        arg136.stPos.z = arg->pos.z;
        arg136.vect.x = 0;
        arg136.vect.y = 20000.0;
        arg136.vect.z = 0;
        arg136.flags = 0;

        _world->ypaworld_func136(&arg136);

        if ( arg136.isect )
            _position.y = arg136.isectPos.y - _overeof;

        ypagun_func128(vec3d(0, 0, 1.0), false);
    }
    else if ( !(arg->field_C & 4) )
    {
        _position.y = arg->pos.y + _pSector->height;
    }

    _old_pos = _position;

    int v12 = 2;
    HandleVisChildrens(&v12);

    return 1;
}

void NC_STACK_ypagun::EnergyInteract(update_msg *arg)
{
    if ( _status != BACT_STATUS_DEAD )
    {
        const float powerEnergyMultiplier = ReadPowerStationEnergyMultiplier();

        if ( _pSector->owner == _owner )
            _energy += powerEnergyMultiplier * _energy_max * (arg->frameTime / 1000.0) * _pSector->energy_power / 40000.0;

        TMobilePowerInfluence mobilePower = _world->FindMobilePowerInfluenceForUnit(this);
        float mobileDelta = powerEnergyMultiplier * _energy_max * (arg->frameTime / 1000.0) * (mobilePower.AlliedEnergyPower - mobilePower.EnemyEnergyPower) / 40000.0;

        if ( mobileDelta >= 0.0 || !IsInvulnerableToDamage() )
            _energy += mobileDelta;

        if ( _energy < 0 )
            _energy = 0;

        if ( _energy > _energy_max )
            _energy = _energy_max;
    }
}

void NC_STACK_ypagun::Renew()
{
    NC_STACK_ypabact::Renew();

    _gunFlags = 0;
    _gunDownTime = 0;

    setBACT_extraViewer(true);

    _viewer_position = vec3d(0, 0, 0);

    _viewer_rotation = _rotation;
}

bool NC_STACK_ypagun::TestTargetSector(const NC_STACK_ypabact * cel_unit) const
{
    vec3d vTgt = cel_unit->_position - GetBodyPosition();

    float dist = vTgt.length();

    if ( dist < 1.0 )
        return 1;

    if ( dist > 2400.0 )
        return 0;

    vTgt /= dist;

    if ( C_PI_2 - clp_acos( vTgt.dot(-_gunRott) ) > _gunMaxUp )
        return 0;

    if ( C_PI_2 - clp_acos( vTgt.dot(_gunRott) ) > _gunMaxDown )
        return 0;

    vec3d tmp = vTgt - _gunRott * vTgt.dot(_gunRott);

    float ln = tmp.length();

    if ( ln <= 0.1 )
        return 1;

    if ( clp_acos( tmp.dot(_gunBasis) / ln ) <= _gunMaxSide )
        return 1;

    return 0;
}

void NC_STACK_ypagun::ypagun_func128(const vec3d &_basis, bool directDown)
{
    _rotation = World::InitialGunRotation(_basis, directDown);
    _gunBasis = _rotation.AxisZ();
    if ( directDown )
        _gunFlags |= GUN_FLAGS_FALLDOWN;
    _gunRott = _rotation.AxisY();
}


vec3d NC_STACK_ypagun::ypagun_func129(const vec3d &axis, float angle)
{
    _gunBasis = mat3x3(axis, -angle, MAT_FLAG_INV_SIN).Transform(_gunBasis);

    _rotation *= mat3x3(axis, angle, MAT_FLAG_INV_SIN);

    return _gunBasis;
}


void NC_STACK_ypagun::setGUN_sideAngle(int angl)
{
    _gunMaxSide = angl / 1000.0;
}

void NC_STACK_ypagun::setGUN_upAngle(int angl)
{
    _gunMaxUp = angl / 1000.0;
}

void NC_STACK_ypagun::setGUN_downAngle(int angl)
{
    _gunMaxDown = angl / 1000.0;
}

void NC_STACK_ypagun::setGUN_fireType(int tp)
{
    _gunType = tp;
    // _isDummy is an attachment-runtime semantic, not a global gun_type side
    // effect. UpdateUnitGuns() sets it only for attached dummy modules so
    // existing standalone/Host-Station gun prototypes keep normal visibility
    // and targetability semantics.
}

void NC_STACK_ypagun::setGUN_fireTime(int time)
{
    _gunFireTime = time;
}

void NC_STACK_ypagun::setGUN_setGround(int grnd)
{
    if ( grnd )
        _gunFlags |= GUN_FLAGS_GROUND;
    else
        _gunFlags &= ~GUN_FLAGS_GROUND;
}

void NC_STACK_ypagun::setGUN_roboGun(int rbo)
{
    if ( rbo )
        _gunFlags |= GUN_FLAGS_ROBO;
    else
        _gunFlags &= ~GUN_FLAGS_ROBO;
}

void NC_STACK_ypagun::setGUN_doesNotFall(int noFall)
{
    if ( noFall )
        _gunFlags |= GUN_FLAGS_NO_FALL;
    else
        _gunFlags &= ~GUN_FLAGS_NO_FALL;
}



int NC_STACK_ypagun::getGUN_sideAngle()
{
    return _gunMaxSide * 1000.0;
}

int NC_STACK_ypagun::getGUN_upAngle()
{
    return _gunMaxUp * 1000.0;
}

int NC_STACK_ypagun::getGUN_downAngle()
{
    return _gunMaxDown * 1000.0;
}

int NC_STACK_ypagun::getGUN_fireType()
{
    return _gunType;
}

int NC_STACK_ypagun::getGUN_fireTime()
{
    return _gunFireTime;
}

bool NC_STACK_ypagun::getGUN_setGround()
{
    return (_gunFlags & GUN_FLAGS_GROUND) != 0;
}

bool NC_STACK_ypagun::IsRoboGun()
{
    return (_gunFlags & GUN_FLAGS_ROBO) != 0;
}
