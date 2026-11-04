#!/usr/bin/env python3
"""The Awakening's speeder bike keeps the physics its def was written for.

- Its def sets friction_angular 5. The expansion's rigid bodies took friction
  up to 10, while Quake 4 drops the whole friction set when a value passes 1,
  which left the bike on the 0.6 fallback and let a turn spin it up. openQ4
  raises the ceiling for the bike alone: stock Quake 4 keeps its ceiling of 1
  (convoy1's landmine sets friction 5, which retail drops).
- rvVehicleRigid applies the def's friction again on restore, so a game saved
  before the fix picks it up without a savegame format change.
- The bike's grip measures the velocity along its own axes. Multiplying by
  the axis turned the velocity by the bike's heading every tic instead.
- Hoverpads honour traceRelativeDirection (the bike's ceiling antisuspensor
  and wall bumpers), derived from the def on spawn and restore, never saved.
"""

from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def read(relative: str) -> str:
    path = ROOT / relative
    if not path.is_file():
        raise AssertionError(f"Required file not found: {path}")
    return path.read_text(encoding="utf-8", errors="replace")


def function_body(source: str, signature: str, context: str) -> str:
    start = source.find(signature)
    if start < 0:
        raise AssertionError(f"Missing {signature!r} in {context}")
    brace = source.find("{", start + len(signature))
    depth = 0
    for index in range(brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[brace + 1:index]
    raise AssertionError(f"Unbalanced body for {signature!r} in {context}")


def require(text: str, needle: str, context: str) -> None:
    if needle not in text:
        raise AssertionError(f"Missing {needle!r} in {context}")


def reject(text: str, pattern: str, context: str) -> None:
    if re.search(pattern, text):
        raise AssertionError(f"Unexpected {pattern!r} in {context}")


def require_order(text: str, needles: tuple[str, ...], context: str) -> None:
    cursor = -1
    for needle in needles:
        index = text.find(needle, cursor + 1)
        if index < 0:
            raise AssertionError(f"Missing {needle!r} after earlier tokens in {context}")
        cursor = index


def check_friction_ceiling() -> None:
    header = read("src/game/physics/Physics_RigidBody.h")
    require(header, "SetFriction( const float linear, const float angular, const float contact, "
                    "const float maxFriction = 1.0f );", "Physics_RigidBody.h")
    source = read("src/game/physics/Physics_RigidBody.cpp")
    body = function_body(source, "void idPhysics_RigidBody::SetFriction(", "Physics_RigidBody.cpp")
    for name in ("linear", "angular", "contact"):
        require(body, f"{name} > maxFriction", "idPhysics_RigidBody::SetFriction")
    reject(body, r">\s*1\.0f", "idPhysics_RigidBody::SetFriction")
    # multiplayer has no Awakening content and keeps Quake 4's rule as it was
    mp = function_body(read("src/mpgame/physics/Physics_RigidBody.cpp"),
                       "void idPhysics_RigidBody::SetFriction(", "mpgame Physics_RigidBody.cpp")
    require(mp, "angular > 1.0f", "mpgame idPhysics_RigidBody::SetFriction")


def check_vehicle_friction() -> None:
    source = read("src/game/vehicle/VehicleRigid.cpp")
    spawn = function_body(source, "void rvVehicleRigid::Spawn( void )", "VehicleRigid.cpp")
    require(spawn, "SetFriction ( );", "rvVehicleRigid::Spawn")
    reject(spawn, r"physicsObj\.SetFriction", "rvVehicleRigid::Spawn")
    helper = function_body(source, "void rvVehicleRigid::SetFriction ( void )", "VehicleRigid.cpp")
    require_order(helper, ('"friction_linear"', '"friction_angular"', '"friction_contact"', "GetMaxFriction ( )"),
                  "rvVehicleRigid::SetFriction")
    ceiling = function_body(source, "float rvVehicleRigid::GetMaxFriction ( void ) const", "VehicleRigid.cpp")
    require(ceiling, "return 1.0f;", "rvVehicleRigid::GetMaxFriction")
    restore = function_body(source, "void rvVehicleRigid::Restore ( idRestoreGame *savefile )", "VehicleRigid.cpp")
    require_order(restore, ("ReadPhysicsObject( physicsObj, this );", "SetFriction ( );", "RestorePhysics"),
                  "rvVehicleRigid::Restore")
    save = function_body(source, "void rvVehicleRigid::Save ( idSaveGame *savefile ) const", "VehicleRigid.cpp")
    reject(save, r"Friction", "rvVehicleRigid::Save")
    header = read("src/game/vehicle/VehicleRigid.h")
    require(header, "virtual float			GetMaxFriction	( void ) const;", "VehicleRigid.h")


def check_speeder_bike() -> None:
    source = read("src/game/awakening/vehicle/SpeederBike.cpp")
    require(source, "static const float SPEEDERBIKE_MAX_FRICTION		= 10.0f;", "SpeederBike.cpp")
    require(source, "virtual float			GetMaxFriction			( void ) const;", "SpeederBike.cpp")
    ceiling = function_body(source, "float riVehicleSpeederBike::GetMaxFriction( void ) const", "SpeederBike.cpp")
    require(ceiling, "return SPEEDERBIKE_MAX_FRICTION;", "riVehicleSpeederBike::GetMaxFriction")
    post = function_body(source, "void riVehicleSpeederBike::RunPostPhysics( void )", "SpeederBike.cpp")
    require(post, "worldVelocity * axis[0], worldVelocity * axis[1], worldVelocity * axis[2]",
            "riVehicleSpeederBike::RunPostPhysics")
    reject(post, r"axis\s*\*\s*physicsObj\.GetLinearVelocity", "riVehicleSpeederBike::RunPostPhysics")
    require(post, "const idVec3 velocity = local * axis;", "riVehicleSpeederBike::RunPostPhysics")


def check_hoverpad_direction() -> None:
    header = read("src/game/vehicle/VehicleParts.h")
    require(header, "idVec3					traceDirection;", "VehicleParts.h")
    require(header, "bool					traceRelative;", "VehicleParts.h")
    source = read("src/game/vehicle/VehicleParts.cpp")
    helper = function_body(source, "void rvVehicleHoverpad::SetTraceDirection ( void )", "VehicleParts.cpp")
    require(helper, '"traceRelativeDirection", "0 0 0"', "rvVehicleHoverpad::SetTraceDirection")
    require(helper, "traceDirection.Normalize ( ) > 0.0f", "rvVehicleHoverpad::SetTraceDirection")
    spawn = function_body(source, "void rvVehicleHoverpad::Spawn ( void )", "VehicleParts.cpp")
    require(spawn, "SetTraceDirection ( );", "rvVehicleHoverpad::Spawn")
    restore = function_body(source, "void rvVehicleHoverpad::Restore ( idRestoreGame* savefile )", "VehicleParts.cpp")
    require(restore, "SetTraceDirection ( );", "rvVehicleHoverpad::Restore")
    save = function_body(source, "void rvVehicleHoverpad::Save ( idSaveGame* savefile ) const", "VehicleParts.cpp")
    reject(save, r"traceDirection|traceRelative", "rvVehicleHoverpad::Save")
    physics = function_body(source, "void rvVehicleHoverpad::RunPhysics ( void )", "VehicleParts.cpp")
    require(physics, "traceRelative ? traceDirection * parent->GetPhysics()->GetAxis() : "
                     "parent->GetPhysics()->GetGravityNormal ( );", "rvVehicleHoverpad::RunPhysics")
    require(physics, "end  = worldOrigin + (probe * height);", "rvVehicleHoverpad::RunPhysics")
    require_order(physics, ("if ( traceRelative ) {", "* -probe - dampingForce;",
                            "} else if ( f < maxRestAngle"), "rvVehicleHoverpad::RunPhysics")


def main() -> None:
    check_friction_ceiling()
    check_vehicle_friction()
    check_speeder_bike()
    check_hoverpad_direction()
    print("awakening_speeder_physics_contract: ok")


if __name__ == "__main__":
    main()
