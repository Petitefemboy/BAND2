#include "includes.h"

Movement g_movement{ };;

enum strafe_dir_t
{
	strafe_forwards = 0,
	strafe_backwards = 180,
	strafe_left = 90,
	strafe_right = -90,
	strafe_back_left = 135,
	strafe_back_right = -135,
	strafe_smooth = 70
};

void Movement::JumpRelated() {
	if (g_cl.m_local->m_MoveType() == MOVETYPE_NOCLIP)
		return;

	if ((g_cl.m_cmd->m_buttons & IN_JUMP) && !(g_cl.m_flags & FL_ONGROUND)) {
		// bhop.
		if (g_menu.main.movement.bhop.get())
			g_cl.m_cmd->m_buttons &= ~IN_JUMP;

		// duck jump ( crate jump ).
		if (g_menu.main.movement.airduck.get())
			g_cl.m_cmd->m_buttons |= IN_DUCK;
	}
}



void Movement::Strafe() {
	if (!g_menu.main.movement.autostrafe.get())
		return;

	if (g_cl.m_local->m_MoveType() == MOVETYPE_NOCLIP || g_cl.m_local->m_MoveType() == MOVETYPE_LADDER || g_cl.m_flags & FL_ONGROUND)
		return;

	vec3_t velocity = g_cl.m_local->m_vecVelocity();
	velocity.z = 0.f;
	float speed = velocity.length_2d();

	if (speed < 10.f || (g_cl.m_buttons & IN_SPEED) || g_input.GetKeyState(g_menu.main.movement.astrafe.get()) || g_input.GetKeyState(g_menu.main.movement.cstrafe.get()))
		return;

	float ideal_strafe = (speed > 5.f) ? math::rad_to_deg(std::asin(15.f / speed)) : 90.f;
	ideal_strafe *= 1.f - (strafe_smooth * 0.01f);
	ideal_strafe = std::min(90.f, ideal_strafe);

	static float switch_key = 1.f;
	static float circle_yaw = 0.f;
	static float old_yaw = 0.f;

	bool holding_w = g_cl.m_cmd->m_buttons & IN_FORWARD;
	bool holding_a = g_cl.m_cmd->m_buttons & IN_MOVELEFT;
	bool holding_s = g_cl.m_cmd->m_buttons & IN_BACK;
	bool holding_d = g_cl.m_cmd->m_buttons & IN_MOVERIGHT;

	bool m_pressing_move = holding_w || holding_a || holding_s || holding_d;

	if (m_pressing_move) {
		float wish_dir = 0.f;

		if (holding_w) {
			if (holding_a)
				wish_dir += (strafe_left / 2);
			else if (holding_d)
				wish_dir += (strafe_right / 2);
			else
				wish_dir += strafe_forwards;
		}
		else if (holding_s) {
			if (holding_a)
				wish_dir += strafe_back_left;
			else if (holding_d)
				wish_dir += strafe_back_right;
			else {
				wish_dir += strafe_backwards;
			}
		}
		else if (holding_a)
			wish_dir += strafe_left;
		else if (holding_d)
			wish_dir += strafe_right;

		g_cl.m_cmd->m_view_angles.y += math::NormalizedAngle(wish_dir);
	}

	float smooth = (1.f - (0.15f * (1.f - strafe_smooth * 0.01f)));

	if (speed <= 0.5f) {
		g_cl.m_cmd->m_side_move = 450.f;
		return;
	}

	float diff = math::NormalizedAngle(g_cl.m_cmd->m_view_angles.y - math::rad_to_deg(std::atan2f(velocity.y, velocity.x)));

	g_cl.m_cmd->m_forward_move = std::clamp((5850.f / speed), -450.f, 450.f);
	g_cl.m_cmd->m_side_move = (diff > 0.f) ? -450.f : 450.f;

	g_cl.m_cmd->m_view_angles.y = math::NormalizedAngle(g_cl.m_cmd->m_view_angles.y - diff * smooth);
}

void Movement::DoPrespeed( ) {
	float   mod, min, max, step, strafe, time, angle;
	vec3_t  plane;

	// min and max values are based on 128 ticks.
	mod = g_csgo.m_globals->m_interval * 128.f;

	// scale min and max based on tickrate.
	min = 2.25f * mod;
	max = 5.f * mod;

	// compute ideal strafe angle for moving in a circle.
	strafe = m_ideal * 2.f;

	// clamp ideal strafe circle value to min and max step.
	math::clamp( strafe, min, max );

	// calculate time.
	time = 320.f / m_speed;

	// clamp time.
	math::clamp( time, 0.35f, 1.f );

	// init step.
	step = strafe;

	while( true ) {
		// if we will not collide with an object or we wont accelerate from such a big step anymore then stop.
		if( !WillCollide( time, step ) || max <= step )
			break;

		// if we will collide with an object with the current strafe step then increment step to prevent a collision.
		step += 0.2f;
	}

	if( step > max ) {
		// reset step.
		step = strafe;

		while( true ) {
			// if we will not collide with an object or we wont accelerate from such a big step anymore then stop.
			if( !WillCollide( time, step ) || step <= -min )
				break;

			// if we will collide with an object with the current strafe step decrement step to prevent a collision.
			step -= 0.2f;
		}

		if( step < -min ) {
			if( GetClosestPlane( plane ) ) {
				// grab the closest object normal
				// compute the angle of the normal
				// and push us away from the object.
				angle = math::rad_to_deg( std::atan2( plane.y, plane.x ) );
				step = -math::NormalizedAngle( m_circle_yaw - angle ) * 0.1f;
			}
		}

		else
			step -= 0.2f;
	}

	else
		step += 0.2f;

	// add the computed step to the steps of the previous circle iterations.
	m_circle_yaw = math::NormalizedAngle( m_circle_yaw + step );

	// apply data to usercmd.
	g_cl.m_cmd->m_view_angles.y = m_circle_yaw;
	g_cl.m_cmd->m_side_move = ( step >= 0.f ) ? -450.f : 450.f;
}

bool Movement::GetClosestPlane( vec3_t &plane ) {
	CGameTrace            trace;
	CTraceFilterWorldOnly filter;
	vec3_t                start{ m_origin };
	float                 smallest{ 1.f };
	const float		      dist{ 75.f };

	// trace around us in a circle
	for( float step{ }; step <= math::pi_2; step += ( math::pi / 10.f ) ) {
		// extend endpoint x units.
		vec3_t end = start;
		end.x += std::cos( step ) * dist;
		end.y += std::sin( step ) * dist;

		g_csgo.m_engine_trace->TraceRay( Ray( start, end, m_mins, m_maxs ), CONTENTS_SOLID, &filter, &trace );

		// we found an object closer, then the previouly found object.
		if( trace.m_fraction < smallest ) {
			// save the normal of the object.
			plane = trace.m_plane.m_normal;
			smallest = trace.m_fraction;
		}
	}

	// did we find any valid object?
	return smallest != 1.f && plane.z < 0.1f;
}

bool Movement::WillCollide( float time, float change ) {
	struct PredictionData_t {
		vec3_t start;
		vec3_t end;
		vec3_t velocity;
		float  direction;
		bool   ground;
		float  predicted;
	};

	PredictionData_t      data;
	CGameTrace            trace;
	CTraceFilterWorldOnly filter;

	// set base data.
	data.ground = g_cl.m_flags & FL_ONGROUND;
	data.start = m_origin;
	data.end = m_origin;
	data.velocity = g_cl.m_local->m_vecVelocity( );
	data.direction = math::rad_to_deg( std::atan2( data.velocity.y, data.velocity.x ) );

	for( data.predicted = 0.f; data.predicted < time; data.predicted += g_csgo.m_globals->m_interval ) {
		// predict movement direction by adding the direction change.
		// make sure to normalize it, in case we go over the -180/180 turning point.
		data.direction = math::NormalizedAngle( data.direction + change );

		// pythagoras.
		float hyp = data.velocity.length_2d( );

		// adjust velocity for new direction.
		data.velocity.x = std::cos( math::deg_to_rad( data.direction ) ) * hyp;
		data.velocity.y = std::sin( math::deg_to_rad( data.direction ) ) * hyp;

		// assume we bhop, set upwards impulse.
		if( data.ground )
			data.velocity.z = g_csgo.sv_jump_impulse->GetFloat( );

		else
			data.velocity.z -= g_csgo.sv_gravity->GetFloat( ) * g_csgo.m_globals->m_interval;

		// we adjusted the velocity for our new direction.
		// see if we can move in this direction, predict our new origin if we were to travel at this velocity.
		data.end += ( data.velocity * g_csgo.m_globals->m_interval );

		// trace
		g_csgo.m_engine_trace->TraceRay( Ray( data.start, data.end, m_mins, m_maxs ), MASK_PLAYERSOLID, &filter, &trace );

		// check if we hit any objects.
		if( trace.m_fraction != 1.f && trace.m_plane.m_normal.z <= 0.9f )
			return true;
		if( trace.m_startsolid || trace.m_allsolid )
			return true;

		// adjust start and end point.
		data.start = data.end = trace.m_endpos;

		// move endpoint 2 units down, and re-trace.
		// do this to check if we are on th floor.
		g_csgo.m_engine_trace->TraceRay( Ray( data.start, data.end - vec3_t{ 0.f, 0.f, 2.f }, m_mins, m_maxs ), MASK_PLAYERSOLID, &filter, &trace );

		// see if we moved the player into the ground for the next iteration.
		data.ground = trace.hit( ) && trace.m_plane.m_normal.z > 0.7f;
	}

	// the entire loop has ran
	// we did not hit shit.
	return false;
}

void Movement::FixMove( CUserCmd *cmd, const ang_t &wish_angles ) {
	vec3_t  move, dir;
	float   delta, len;
	ang_t   move_angle;

	// roll nospread fix.
	if( !( g_cl.m_flags & FL_ONGROUND ) && cmd->m_view_angles.z != 0.f )
		cmd->m_side_move = 0.f;

	// convert movement to vector.
	move = { cmd->m_forward_move, cmd->m_side_move, 0.f };

	// get move length and ensure we're using a unit vector ( vector with length of 1 ).
	len = move.normalize( );
	if( !len )
		return;

	// convert move to an angle.
	math::VectorAngles( move, move_angle );

	// calculate yaw delta.
	delta = ( cmd->m_view_angles.y - wish_angles.y );

	// accumulate yaw delta.
	move_angle.y += delta;

	// calculate our new move direction.
	// dir = move_angle_forward * move_length
	math::AngleVectors( move_angle, &dir );

	// scale to og movement.
	dir *= len;

	// strip old flags.
	g_cl.m_cmd->m_buttons &= ~( IN_FORWARD | IN_BACK | IN_MOVELEFT | IN_MOVERIGHT );

	// fix ladder and noclip.
	if( g_cl.m_local->m_MoveType( ) == MOVETYPE_LADDER ) {
		// invert directon for up and down.
		if( cmd->m_view_angles.x >= 45.f && wish_angles.x < 45.f && std::abs( delta ) <= 65.f )
			dir.x = -dir.x;

		// write to movement.
		cmd->m_forward_move = dir.x;
		cmd->m_side_move = dir.y;

		// set new button flags.
		if( cmd->m_forward_move > 200.f )
			cmd->m_buttons |= IN_FORWARD;

		else if( cmd->m_forward_move < -200.f )
			cmd->m_buttons |= IN_BACK;

		if( cmd->m_side_move > 200.f )
			cmd->m_buttons |= IN_MOVERIGHT;

		else if( cmd->m_side_move < -200.f )
			cmd->m_buttons |= IN_MOVELEFT;
	}

	// we are moving normally.
	else {
		// we must do this for pitch angles that are out of bounds.
		if( cmd->m_view_angles.x < -90.f || cmd->m_view_angles.x > 90.f )
			dir.x = -dir.x;

		// set move.
		cmd->m_forward_move = dir.x;
		cmd->m_side_move = dir.y;

		// set new button flags.
		if( cmd->m_forward_move > 0.f )
			cmd->m_buttons |= IN_FORWARD;

		else if( cmd->m_forward_move < 0.f )
			cmd->m_buttons |= IN_BACK;

		if( cmd->m_side_move > 0.f )
			cmd->m_buttons |= IN_MOVERIGHT;

		else if( cmd->m_side_move < 0.f )
			cmd->m_buttons |= IN_MOVELEFT;
	}
}

void Movement::AutoPeek( ) {
	// set to invert if we press the button.
	if( g_input.GetKeyState( g_menu.main.movement.autopeek.get( ) ) ) {
		if( g_cl.m_old_shot )
			m_invert = true;

		vec3_t move{ g_cl.m_cmd->m_forward_move, g_cl.m_cmd->m_side_move, 0.f };

		if( m_invert ) {
			move *= -1.f;
			g_cl.m_cmd->m_forward_move = move.x;
			g_cl.m_cmd->m_side_move = move.y;
		}
	}

	else m_invert = false;

	bool can_stop = g_menu.main.movement.autostop_always_on.get( ) || ( !g_menu.main.movement.autostop_always_on.get( ) && g_input.GetKeyState( g_menu.main.movement.autostop.get( ) ) );
	if( ( g_input.GetKeyState( g_menu.main.movement.autopeek.get( ) ) || can_stop ) && g_aimbot.m_stop ) {
		Movement::QuickStop( );
	}
}

void Movement::QuickStop( ) {
	// convert velocity to angular momentum.
	ang_t angle;
	math::VectorAngles( g_cl.m_local->m_vecVelocity( ), angle );

	// get our current speed of travel.
	float speed = g_cl.m_local->m_vecVelocity( ).length( );

	// fix direction by factoring in where we are looking.
	angle.y = g_cl.m_view_angles.y - angle.y;

	// convert corrected angle back to a direction.
	vec3_t direction;
	math::AngleVectors( angle, &direction );

	vec3_t stop = direction * -speed;

	if( g_cl.m_speed > 13.f ) {
		g_cl.m_cmd->m_forward_move = stop.x;
		g_cl.m_cmd->m_side_move = stop.y;
	}
	else {
		g_cl.m_cmd->m_forward_move = 0.f;
		g_cl.m_cmd->m_side_move = 0.f;
	}
}

void Movement::FakeWalk( ) {
	vec3_t velocity{ g_cl.m_local->m_vecVelocity( ) };
	int    ticks{ }, max{ 16 };

	if( !g_input.GetKeyState( g_menu.main.movement.fakewalk.get( ) ) )
		return;

	if( !g_cl.m_local->GetGroundEntity( ) )
		return;

	// user was running previously and abrubtly held the fakewalk key
	// we should quick-stop under this circumstance to hit the 0.22 flick
	// perfectly, and speed up our fakewalk after running even more.
	//if( g_cl.m_initial_flick ) {
	//	Movement::QuickStop( );
	//	return;
	//}
	
	// reference:
	// https://github.com/ValveSoftware/source-sdk-2013/blob/master/mp/src/game/shared/gamemovement.cpp#L1612

	// calculate friction.
	float friction = g_csgo.sv_friction->GetFloat( ) * g_cl.m_local->m_surfaceFriction( );

	for( ; ticks < g_cl.m_max_lag; ++ticks ) {
		// calculate speed.
		float speed = velocity.length( );

		// if too slow return.
		if( speed <= 0.1f )
			break;

		// bleed off some speed, but if we have less than the bleed, threshold, bleed the threshold amount.
		float control = std::max( speed, g_csgo.sv_stopspeed->GetFloat( ) );

		// calculate the drop amount.
		float drop = control * friction * g_csgo.m_globals->m_interval;

		// scale the velocity.
		float newspeed = std::max( 0.f, speed - drop );

		if( newspeed != speed ) {
			// determine proportion of old speed we are using.
			newspeed /= speed;

			// adjust velocity according to proportion.
			velocity *= newspeed;
		}
	}

	// zero forwardmove and sidemove.
	if( ticks > ( ( max - 1 ) - g_csgo.m_cl->m_choked_commands ) || !g_csgo.m_cl->m_choked_commands ) {
		g_cl.m_cmd->m_forward_move = g_cl.m_cmd->m_side_move = 0.f;
	}
}