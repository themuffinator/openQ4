// openQ4: the Strogg chaingun turret venting an overheated barrel
// (idVehicleTurretGun "fx_overheat", played at the muzzle when the gun
// locks out). Hot white steam jets from the barrel and rises, with a spit
// of embers from the breech.
effect effects/openq4/awakening/turret_overheat
{
	size	96

	emitter "steam"
	{
		duration	1.8,1.8
		count		36,36

		sprite
		{
			duration	0.9,1.5
			material	"gfx/effects/smoke/cloud_alpha2"
			gravity		-0.35,-0.5

			start
			{
				velocity { box 18,-6,-4,40,6,10 }
				size { point 4,4 }
				tint { line 0.92,0.9,0.86,0.8,0.78,0.75 }
				fade { point 0.55 }
				rotate { box 0,1 }
			}

			motion
			{
				size { envelope "linear" }
				fade { envelope "linear" }
				rotate { envelope "linear" }
			}

			end
			{
				size { box 16,16,26,26 }
				fade { point 0 }
				rotate { box -0.4,0.4 relative }
			}
		}
	}
	emitter "haze"
	{
		duration	1.2,1.2
		count		10,10

		line
		{
			duration	0.6,0.9
			material	"gfx/effects/smoke/soft_alpha_trail"

			start
			{
				velocity { box 6,-2,4,14,2,12 }
				size { box 2,3 }
				tint { point 0.85,0.83,0.8 }
				fade { point 0.4 }
				length { point 10,0,0 }
			}

			motion
			{
				size { envelope "linear" }
				fade { envelope "linear" }
			}

			end
			{
				size { box 6,9 }
				fade { point 0 }
			}
		}
	}
	spawner "embers"
	{
		count		8,12

		line
		{
			duration	0.25,0.45
			material	"gfx/effects/energy_sparks/spark_line2"
			gravity		0.6,0.9

			start
			{
				velocity { box 20,-30,0,60,30,40 }
				size { box 0.4,0.7 }
				tint { line 1,0.62,0.25,1,0.42,0.1 }
				fade { point 1 }
				length { point 3,0,0 }
			}

			motion
			{
				fade { envelope "linear" }
			}

			end
			{
				fade { point 0 }
			}
		}
	}
}
