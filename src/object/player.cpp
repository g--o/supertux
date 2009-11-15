//  SuperTux
//  Copyright (C) 2006 Matthias Braun <matze@braunis.de>
//
//  This program is free software; you can redistribute it and/or
//  modify it under the terms of the GNU General Public License
//  as published by the Free Software Foundation; either version 2
//  of the License, or (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program; if not, write to the Free Software
//  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
#include <config.h>

#include "object/player.hpp"

#include "audio/sound_manager.hpp"
#include "badguy/badguy.hpp"
#include "control/codecontroller.hpp"
#include "control/joystickkeyboardcontroller.hpp"
#include "object/display_effect.hpp"
#include "util/log.hpp"
#include "object/falling_coin.hpp"
#include "supertux/game_session.hpp"
#include "util/gettext.hpp"
#include "supertux/main.hpp"
#include "object/bullet.hpp"
#include "object/camera.hpp"
#include "object/portable.hpp"
#include "object/sprite_particle.hpp"
#include "object/tilemap.hpp"
#include "object/particles.hpp"
#include "supertux/player_status.hpp"
#include "math/random_generator.hpp"
#include "supertux/sector.hpp"
#include "scripting/squirrel_util.hpp"
#include "sprite/sprite.hpp"
#include "sprite/sprite_manager.hpp"
#include "supertux/tile.hpp"
#include "trigger/climbable.hpp"

#include <typeinfo>
#include <cmath>
#include <iostream>
#include <cassert>

//#define SWIMMING

namespace {
  static const int TILES_FOR_BUTTJUMP = 3;
  static const float BUTTJUMP_MIN_VELOCITY_Y = 400.0f;
  static const float SHOOTING_TIME = .150f;

  /** number of idle stages, including standing */
  static const unsigned int IDLE_STAGE_COUNT = 5;
  /**
   * how long to play each idle animation in milliseconds
   * '0' means the sprite action is played once before moving onto the next
   * animation
   */
  static const int IDLE_TIME[] = { 5000, 0, 2500, 0, 2500 };
  /** idle stages */
  static const std::string IDLE_STAGES[] =
    { "stand",
      "idle",
      "stand",
      "idle",
      "stand" };

  /** acceleration in horizontal direction when walking
   * (all accelerations are in  pixel/s^2) */
  static const float WALK_ACCELERATION_X = 300;
  /** acceleration in horizontal direction when running */ 
  static const float RUN_ACCELERATION_X = 400;
  /** acceleration when skidding */
  static const float SKID_XM = 200;
  /** time of skidding in seconds */
  static const float SKID_TIME = .3f;
  /** maximum walk velocity (pixel/s) */
  static const float MAX_WALK_XM = 230;
  /** maximum run velocity (pixel/s) */
  static const float MAX_RUN_XM = 320;
  /** maximum horizontal climb velocity */
  static const float MAX_CLIMB_XM = 48;
  /** maximum vertical climb velocity */
  static const float MAX_CLIMB_YM = 128;
  /** instant velocity when tux starts to walk */
  static const float WALK_SPEED = 100;

  /** multiplied by WALK_ACCELERATION to give friction */
  static const float NORMAL_FRICTION_MULTIPLIER = 1.5f;
  /** multiplied by WALK_ACCELERATION to give friction */
  static const float ICE_FRICTION_MULTIPLIER = 0.1f;
  static const float ICE_ACCELERATION_MULTIPLIER = 0.25f;

  /** time of the kick (kicking mriceblock) animation */
  static const float KICK_TIME = .3f;
  /** time of tux cheering (currently unused) */
  static const float CHEER_TIME = 1.0f;

  /** if Tux cannot unduck for this long, he will get hurt */
  static const float UNDUCK_HURT_TIME = 0.25f;
  /** gravity is higher after the jump key is released before
      the apex of the jump is reached */
  static const float JUMP_EARLY_APEX_FACTOR = 3.0;

  static const float JUMP_GRACE_TIME = 0.25f; /**< time before hitting the ground that the jump button may be pressed (and still trigger a jump) */

  bool no_water = true;
}

Player::Player(PlayerStatus* _player_status, const std::string& name)
  : scripting_controller(0), 
    player_status(_player_status), 
    scripting_controller_old(0),
    grabbed_object(NULL), ghost_mode(false), edit_mode(false), idle_stage(0),
    climbing(0)
{
  this->name = name;
  controller = main_controller;
  scripting_controller = new CodeController();
  sprite = sprite_manager->create("images/creatures/tux/tux.sprite");
  airarrow.reset(new Surface("images/engine/hud/airarrow.png"));
  idle_timer.start(IDLE_TIME[0]/1000.0f);

  sound_manager->preload("sounds/bigjump.wav");
  sound_manager->preload("sounds/jump.wav");
  sound_manager->preload("sounds/hurt.wav");
  sound_manager->preload("sounds/skid.wav");
  sound_manager->preload("sounds/flip.wav");
  sound_manager->preload("sounds/invincible_start.ogg");
  sound_manager->preload("sounds/splash.ogg");

  init();
}

Player::~Player()
{
  if (climbing) stop_climbing(*climbing);
  delete sprite;
  delete scripting_controller;
}

void
Player::init()
{
  if(is_big())
    set_size(31.8f, 62.8f);
  else
    set_size(31.8f, 30.8f);

  dir = RIGHT;
  old_dir = dir;
  duck = false;
  dead = false;

  dying = false;
  peekingX = AUTO;
  peekingY = AUTO;
  last_ground_y = 0;
  fall_mode = ON_GROUND;
  jumping = false;
  jump_early_apex = false;
  can_jump = true;
  wants_buttjump = false;
  does_buttjump = false;
  growing = false;
  deactivated = false;
  backflipping = false;
  backflip_direction = 0;
  visible = true;
  swimming = false;
  on_ice = false;
  ice_this_frame = false;
  speedlimit = 0; //no special limit

  on_ground_flag = false;
  grabbed_object = NULL;

  climbing = 0;

  physic.reset();
}

void
Player::expose(HSQUIRRELVM vm, SQInteger table_idx)
{
  if (name.empty())
    return;

  Scripting::expose_object(vm, table_idx, dynamic_cast<Scripting::Player *>(this), name, false);
}

void
Player::unexpose(HSQUIRRELVM vm, SQInteger table_idx)
{
  if (name.empty())
    return;

  Scripting::unexpose_object(vm, table_idx, name);
}

float
Player::get_speedlimit()
{
  return speedlimit;
}

void
Player::set_speedlimit(float newlimit)
{
  speedlimit=newlimit;
}

void
Player::set_controller(Controller* controller)
{
  this->controller = controller;
}

void 
Player::use_scripting_controller(bool use_or_release)
{
  if ((use_or_release == true) && (controller != scripting_controller)) {
    scripting_controller_old = get_controller();
    set_controller(scripting_controller);
  }
  if ((use_or_release == false) && (controller == scripting_controller)) {
    set_controller(scripting_controller_old);
    scripting_controller_old = 0;
  }
}

void 
Player::do_scripting_controller(std::string control, bool pressed)
{
  for(int i = 0; Controller::controlNames[i] != 0; ++i) {
    if(control == std::string(Controller::controlNames[i])) {
      scripting_controller->press(Controller::Control(i), pressed);
    }
  }
}

bool
Player::adjust_height(float new_height)
{
  Rect bbox2 = bbox;
  bbox2.move(Vector(0, bbox.get_height() - new_height));
  bbox2.set_height(new_height);

  if(new_height > bbox.get_height()) {
    Rect additional_space = bbox2;
    additional_space.set_height(new_height - bbox.get_height());
    if(!Sector::current()->is_free_of_statics(additional_space, this, true))
      return false;
  }

  // adjust bbox accordingly
  // note that we use members of moving_object for this, so we can run this during CD, too
  set_pos(bbox2.p1);
  set_size(bbox2.get_width(), bbox2.get_height());
  return true;
}

void
Player::trigger_sequence(std::string sequence_name)
{
  if (climbing) stop_climbing(*climbing);
  GameSession::current()->start_sequence(sequence_name);
}

void
Player::update(float elapsed_time)
{
  if( no_water ){
    swimming = false;
  }
  no_water = true;

  if(dying && dying_timer.check()) {
    dead = true;
    return;
  }

  if(!dying && !deactivated)
    handle_input();

  // handle_input() calls apply_friction() when Tux is not walking, so we'll have to do this ourselves
  if (deactivated)
    apply_friction();

  // extend/shrink tux collision rectangle so that we fall through/walk over 1
  // tile holes
  if(fabsf(physic.get_velocity_x()) > MAX_WALK_XM) {
    set_width(34);
  } else {
    set_width(31.8f);
  }

  // on downward slopes, adjust vertical velocity so tux walks smoothly down
  if (on_ground()) {
    if(floor_normal.y != 0) {
      if ((floor_normal.x * physic.get_velocity_x()) >= 0) {
        physic.set_velocity_y(250);
      }
    }
  }

  // handle backflipping
  if (backflipping) {
    //prevent player from changing direction when backflipping
    dir = (backflip_direction == 1) ? LEFT : RIGHT;
    if (backflip_timer.started()) physic.set_velocity_x(100 * backflip_direction);
  }

  // set fall mode...
  if(on_ground()) {
    fall_mode = ON_GROUND;
    last_ground_y = get_pos().y;
  } else {
    if(get_pos().y > last_ground_y)
      fall_mode = FALLING;
    else if(fall_mode == ON_GROUND)
      fall_mode = JUMPING;
  }

  // check if we landed
  if(on_ground()) {
    jumping = false;
    if (backflipping && (!backflip_timer.started())) {
      backflipping = false;
      backflip_direction = 0;

      // if controls are currently deactivated, we take care of standing up ourselves
      if (deactivated)
        do_standup();
    }
  }

  // calculate movement for this frame
  movement = physic.get_movement(elapsed_time);

  if(grabbed_object != NULL && !dying) {
    Vector pos = get_pos() +
      Vector(dir == LEFT ? -16 : 16, get_bbox().get_height()*0.66666 - 32);
    grabbed_object->grab(*this, pos, dir);
  }

  if(grabbed_object != NULL && dying){
    grabbed_object->ungrab(*this, dir);
    grabbed_object = NULL;
  }

  if(!ice_this_frame && on_ground())
    on_ice = false;

  on_ground_flag = false;
  ice_this_frame = false;

  // when invincible, spawn particles
  if (invincible_timer.started() && !dying)
  {
    if (systemRandom.rand(0, 2) == 0) {
      float px = systemRandom.randf(bbox.p1.x+0, bbox.p2.x-0);
      float py = systemRandom.randf(bbox.p1.y+0, bbox.p2.y-0);
      Vector ppos = Vector(px, py);
      Vector pspeed = Vector(0, 0);
      Vector paccel = Vector(0, 0);
      Sector::current()->add_object(new SpriteParticle("images/objects/particles/sparkle.sprite", 
         // draw bright sparkle when there is lots of time left, dark sparkle when invincibility is about to end
         (invincible_timer.get_timeleft() > TUX_INVINCIBLE_TIME_WARNING) ?
              // make every other a longer sparkle to make trail a bit fuzzy
              (size_t(game_time*20)%2) ? "small" : "medium"
         :
              "dark", ppos, ANCHOR_MIDDLE, pspeed, paccel, LAYER_OBJECTS+1+5));
    }
  }

  if (growing) {
    if (sprite->animation_done()) growing = false;
  }

}

bool
Player::on_ground()
{
  return on_ground_flag;
}

bool
Player::is_big()
{
  if(player_status->bonus == NO_BONUS)
    return false;

  return true;
}

void
Player::apply_friction()
{
  if ((on_ground()) && (fabs(physic.get_velocity_x()) < WALK_SPEED)) {
    physic.set_velocity_x(0);
    physic.set_acceleration_x(0);
  } else {
    float friction = WALK_ACCELERATION_X * (on_ice ? ICE_FRICTION_MULTIPLIER : NORMAL_FRICTION_MULTIPLIER);
    if(physic.get_velocity_x() < 0) {
      physic.set_acceleration_x(friction);
    } else if(physic.get_velocity_x() > 0) {
      physic.set_acceleration_x(-friction);
    } // no friction for physic.get_velocity_x() == 0
  }
}

void
Player::handle_horizontal_input()
{
  float vx = physic.get_velocity_x();
  float vy = physic.get_velocity_y();
  float ax = physic.get_acceleration_x();
  float ay = physic.get_acceleration_y();

  float dirsign = 0;
  if(!duck || physic.get_velocity_y() != 0) {
    if(controller->hold(Controller::LEFT) && !controller->hold(Controller::RIGHT)) {
      old_dir = dir;
      dir = LEFT;
      dirsign = -1;
    } else if(!controller->hold(Controller::LEFT)
              && controller->hold(Controller::RIGHT)) {
      old_dir = dir;
      dir = RIGHT;
      dirsign = 1;
    }
  }

  // do not run if action key is pressed or we're holding something
  // so tux can only walk while shooting
  if ( controller->hold(Controller::ACTION) || grabbed_object ) {
    ax = dirsign * WALK_ACCELERATION_X;
    // limit speed
    if(vx >= MAX_WALK_XM && dirsign > 0) {
      vx = MAX_WALK_XM;
      ax = 0;
    } else if(vx <= -MAX_WALK_XM && dirsign < 0) {
      vx = -MAX_WALK_XM;
      ax = 0;
    }
  } else {
    if( vx * dirsign < MAX_WALK_XM ) {
      ax = dirsign * WALK_ACCELERATION_X;
    } else {
      ax = dirsign * RUN_ACCELERATION_X;
    }
    // limit speed
    if(vx >= MAX_RUN_XM && dirsign > 0) {
      vx = MAX_RUN_XM;
      ax = 0;
    } else if(vx <= -MAX_RUN_XM && dirsign < 0) {
      vx = -MAX_RUN_XM;
      ax = 0;
    }
  }

  // we can reach WALK_SPEED without any acceleration
  if(dirsign != 0 && fabs(vx) < WALK_SPEED) {
    vx = dirsign * WALK_SPEED;
  }

  //Check speedlimit.
  if( speedlimit > 0 &&  vx * dirsign >= speedlimit ) {
      vx = dirsign * speedlimit;
      ax = 0;
  }

  // changing directions?
  if(on_ground() && ((vx < 0 && dirsign >0) || (vx>0 && dirsign<0))) {
    // let's skid!
    if(fabs(vx)>SKID_XM && !skidding_timer.started()) {
      skidding_timer.start(SKID_TIME);
      sound_manager->play("sounds/skid.wav");
      // dust some particles
      Sector::current()->add_object(
        new Particles(
          Vector(dir == RIGHT ? get_bbox().p2.x : get_bbox().p1.x, get_bbox().p2.y),
          dir == RIGHT ? 270+20 : 90-40, dir == RIGHT ? 270+40 : 90-20,
          Vector(280, -260), Vector(0, 300), 3, Color(.4f, .4f, .4f), 3, .8f,
          LAYER_OBJECTS+1));

      ax *= 2.5;
    } else {
      ax *= 2;
    }
  }

  if(on_ice) {
    ax *= ICE_ACCELERATION_MULTIPLIER;
  }

  physic.set_velocity(vx, vy);
  physic.set_acceleration(ax, ay);

  // we get slower when not pressing any keys
  if(dirsign == 0) {
    apply_friction();
  }

}

void
Player::do_cheer()
{
  do_duck();
  do_backflip();
  do_standup();
}

void
Player::do_duck() {
  if (duck)
    return;
  if (!is_big())
    return;

  if (physic.get_velocity_y() != 0)
    return;
  if (!on_ground())
    return;
  if (does_buttjump)
    return;

  if (adjust_height(31.8f)) {
    duck = true;
    growing = false;
    unduck_hurt_timer.stop();
  } else {
    // FIXME: what now?
  }
}

void
Player::do_standup() {
  if (!duck)
    return;
  if (!is_big())
    return;
  if (backflipping)
    return;

  if (adjust_height(63.8f)) {
    duck = false;
    unduck_hurt_timer.stop();
  } else {
    // if timer is not already running, start it.
    if (unduck_hurt_timer.get_period() == 0) {
      unduck_hurt_timer.start(UNDUCK_HURT_TIME);
    }
    else if (unduck_hurt_timer.check()) {
      kill(false);
    }
  }

}

void
Player::do_backflip() {
  if (!duck)
    return;
  if (!on_ground())
    return;

  backflip_direction = (dir == LEFT)?(+1):(-1);
  backflipping = true;
  do_jump(-580);
  sound_manager->play("sounds/flip.wav");
  backflip_timer.start(0.15f);
}

void
Player::do_jump(float yspeed) {
  if (!on_ground())
    return;

  physic.set_velocity_y(yspeed);
  //bbox.move(Vector(0, -1));
  jumping = true;
  on_ground_flag = false;
  can_jump = false;

  // play sound
  if (is_big()) {
    sound_manager->play("sounds/bigjump.wav");
  } else {
    sound_manager->play("sounds/jump.wav");
  }
}

void
Player::early_jump_apex() {
  if(jump_early_apex) {
    return;
  }
  jump_early_apex = true;
  physic.set_gravity(physic.get_gravity() * JUMP_EARLY_APEX_FACTOR);
}

void
Player::do_jump_apex() {
  if(!jump_early_apex) {
    return;
  }
  jump_early_apex = false;
  physic.set_gravity(physic.get_gravity() / JUMP_EARLY_APEX_FACTOR);
}

void
Player::handle_vertical_input()
{
  // Press jump key
  if(controller->pressed(Controller::JUMP)) jump_button_timer.start(JUMP_GRACE_TIME);
  if(controller->hold(Controller::JUMP) && jump_button_timer.started() && can_jump) {
    jump_button_timer.stop();
    if (duck) {
      // when running, only jump a little bit; else do a backflip
      if ((physic.get_velocity_x() != 0) || (controller->hold(Controller::LEFT)) || (controller->hold(Controller::RIGHT))) do_jump(-300); else do_backflip();
    } else {
      // jump a bit higher if we are running; else do a normal jump
      if (fabs(physic.get_velocity_x()) > MAX_WALK_XM) do_jump(-580); else do_jump(-520);
    }
  }
  // Let go of jump key
  else if(!controller->hold(Controller::JUMP)) {
    if (!backflipping && jumping && physic.get_velocity_y() < 0) {
      jumping = false;
      early_jump_apex();
    }
  }

  if(jump_early_apex && physic.get_velocity_y() >= 0) {
    do_jump_apex();
  }

  /* In case the player has pressed Down while in a certain range of air,
     enable butt jump action */
  if (controller->hold(Controller::DOWN) && !duck && is_big() && !on_ground()) {
    wants_buttjump = true;
    if (physic.get_velocity_y() >= BUTTJUMP_MIN_VELOCITY_Y) does_buttjump = true;
  }

  /* When Down is not held anymore, disable butt jump */
  if(!controller->hold(Controller::DOWN)) {
    wants_buttjump = false;
    does_buttjump = false;
  }

  // swimming
  physic.set_acceleration_y(0);
#ifdef SWIMMING
  if (swimming) {
    if (controller->hold(Controller::UP) || controller->hold(Controller::JUMP))
      physic.set_acceleration_y(-2000);
    physic.set_velocity_y(physic.get_velocity_y() * 0.94);
  }
#endif
}

void
Player::handle_input()
{
  if (ghost_mode) {
    handle_input_ghost();
    return;
  }
  if (climbing) {
    handle_input_climbing();
    return;
  }

  /* Peeking */
  if( controller->released( Controller::PEEK_LEFT ) || controller->released( Controller::PEEK_RIGHT ) ) {
    peekingX = AUTO;
  }
  if( controller->released( Controller::PEEK_UP ) || controller->released( Controller::PEEK_DOWN ) ) {
    peekingY = AUTO;
  }
  if( controller->pressed( Controller::PEEK_LEFT ) ) {
    peekingX = LEFT;
  }
  if( controller->pressed( Controller::PEEK_RIGHT ) ) {
    peekingX = RIGHT;
  }
  if(!backflipping && !jumping && on_ground()) {
    if( controller->pressed( Controller::PEEK_UP ) ) {
      peekingY = UP;
    } else if( controller->pressed( Controller::PEEK_DOWN ) ) {
      peekingY = DOWN;
    }
  }

  /* Handle horizontal movement: */
  if (!backflipping) handle_horizontal_input();

  /* Jump/jumping? */
  if (on_ground())
    can_jump = true;

  /* Handle vertical movement: */
  handle_vertical_input();

  /* Shoot! */
  if (controller->pressed(Controller::ACTION) && (player_status->bonus == FIRE_BONUS || player_status->bonus == ICE_BONUS)) {
    if(Sector::current()->add_bullet(
         get_pos() + ((dir == LEFT)? Vector(0, bbox.get_height()/2)
                      : Vector(32, bbox.get_height()/2)),
         physic.get_velocity_x(), dir))
      shooting_timer.start(SHOOTING_TIME);
  }

  /* Duck or Standup! */
  if (controller->hold(Controller::DOWN)) {
    do_duck();
  } else {
    do_standup();
  }

  /* grabbing */
  try_grab();

  if(!controller->hold(Controller::ACTION) && grabbed_object) {
    // move the grabbed object a bit away from tux
    Vector pos = get_pos() +
        Vector(dir == LEFT ? -bbox.get_width()-1 : bbox.get_width()+1,
                bbox.get_height()*0.66666 - 32);
    Rect dest(pos, pos + Vector(32, 32));
    if(Sector::current()->is_free_of_movingstatics(dest)) {
      MovingObject* moving_object = dynamic_cast<MovingObject*> (grabbed_object);
      if(moving_object) {
        moving_object->set_pos(pos);
      } else {
        log_debug << "Non MovingObject grabbed?!?" << std::endl;
      }
      if(controller->hold(Controller::UP)) {
        grabbed_object->ungrab(*this, UP);
      } else {
        grabbed_object->ungrab(*this, dir);
      }
      grabbed_object = NULL;
    }
  }
}

void
Player::try_grab()
{
  if(controller->hold(Controller::ACTION) && !grabbed_object
      && !duck) {
  Sector* sector = Sector::current();
    Vector pos;
    if(dir == LEFT) {
      pos = Vector(bbox.get_left() - 5, bbox.get_bottom() - 16);
    } else {
      pos = Vector(bbox.get_right() + 5, bbox.get_bottom() - 16);
    }

    for(Sector::Portables::iterator i = sector->portables.begin();
        i != sector->portables.end(); ++i) {
      Portable* portable = *i;
      if(!portable->is_portable())
        continue;

      // make sure the Portable is a MovingObject
      MovingObject* moving_object = dynamic_cast<MovingObject*> (portable);
      assert(moving_object);
      if(moving_object == NULL)
        continue;

      // make sure the Portable isn't currently non-solid
      if(moving_object->get_group() == COLGROUP_DISABLED) continue;

      // check if we are within reach
      if(moving_object->get_bbox().contains(pos)) {
        if (climbing) stop_climbing(*climbing);
        grabbed_object = portable;
        grabbed_object->grab(*this, get_pos(), dir);
        break;
      }
    }
  }
}

void
Player::handle_input_ghost()
{
  float vx = 0;
  float vy = 0;
  if (controller->hold(Controller::LEFT)) {
    dir = LEFT;
    vx -= MAX_RUN_XM * 2;
  }
  if (controller->hold(Controller::RIGHT)) {
    dir = RIGHT;
    vx += MAX_RUN_XM * 2;
  }
  if ((controller->hold(Controller::UP)) || (controller->hold(Controller::JUMP))) {
    vy -= MAX_RUN_XM * 2;
  }
  if (controller->hold(Controller::DOWN)) {
    vy += MAX_RUN_XM * 2;
  }
  if (controller->hold(Controller::ACTION)) {
    set_ghost_mode(false);
  }
  physic.set_velocity(vx, vy);
  physic.set_acceleration(0, 0);
}

void
Player::add_coins(int count)
{
  player_status->add_coins(count);
}

int
Player::get_coins()
{
  return player_status->coins;
}

bool
Player::add_bonus(const std::string& bonustype)
{
  BonusType type = NO_BONUS;

  if(bonustype == "grow") {
    type = GROWUP_BONUS;
  } else if(bonustype == "fireflower") {
    type = FIRE_BONUS;
  } else if(bonustype == "iceflower") {
    type = ICE_BONUS;
  } else if(bonustype == "none") {
    type = NO_BONUS;
  } else {
    std::ostringstream msg;
    msg << "Unknown bonus type "  << bonustype;
    throw std::runtime_error(msg.str());
  }

  return add_bonus(type);
}

bool
Player::add_bonus(BonusType type, bool animate)
{
  // always ignore NO_BONUS
  if (type == NO_BONUS) {
    return true;
  }

  // ignore GROWUP_BONUS if we're already big
  if (type == GROWUP_BONUS) {
    if (player_status->bonus == GROWUP_BONUS)
      return true;
    if (player_status->bonus == FIRE_BONUS)
      return true;
    if (player_status->bonus == ICE_BONUS)
      return true;
  }

  return set_bonus(type, animate);
}

bool
Player::set_bonus(BonusType type, bool animate)
{
  if(player_status->bonus == NO_BONUS) {
    if (!adjust_height(62.8f)) {
      printf("can't adjust\n");
      return false;
    }
    if(animate) {
      growing = true;
      sprite->set_action((dir == LEFT)?"grow-left":"grow-right", 1);
    }
    if (climbing) stop_climbing(*climbing);
  }

  if (type == NO_BONUS) {
    if (does_buttjump) does_buttjump = false;
  }

  if ((type == NO_BONUS) || (type == GROWUP_BONUS)) {
    if ((player_status->bonus == FIRE_BONUS) && (animate)) {
      // visually lose helmet
      Vector ppos = Vector((bbox.p1.x + bbox.p2.x) / 2, bbox.p1.y);
      Vector pspeed = Vector(((dir==LEFT) ? +100 : -100), -300);
      Vector paccel = Vector(0, 1000);
      std::string action = (dir==LEFT)?"left":"right";
      Sector::current()->add_object(new SpriteParticle("images/objects/particles/firetux-helmet.sprite", action, ppos, ANCHOR_TOP, pspeed, paccel, LAYER_OBJECTS-1));
      if (climbing) stop_climbing(*climbing);
    }
    if ((player_status->bonus == ICE_BONUS) && (animate)) {
      // visually lose cap
      Vector ppos = Vector((bbox.p1.x + bbox.p2.x) / 2, bbox.p1.y);
      Vector pspeed = Vector(((dir==LEFT) ? +100 : -100), -300);
      Vector paccel = Vector(0, 1000);
      std::string action = (dir==LEFT)?"left":"right";
      Sector::current()->add_object(new SpriteParticle("images/objects/particles/icetux-cap.sprite", action, ppos, ANCHOR_TOP, pspeed, paccel, LAYER_OBJECTS-1));
      if (climbing) stop_climbing(*climbing);
    }
    player_status->max_fire_bullets = 0;
    player_status->max_ice_bullets = 0;
  }
  if (type == FIRE_BONUS) player_status->max_fire_bullets++;
  if (type == ICE_BONUS) player_status->max_ice_bullets++;

  player_status->bonus = type;
  return true;
}

void
Player::set_visible(bool visible)
{
  this->visible = visible;
  if( visible )
    set_group(COLGROUP_MOVING);
  else
    set_group(COLGROUP_DISABLED);
}

bool
Player::get_visible()
{
  return visible;
}

void
Player::kick()
{
  kick_timer.start(KICK_TIME);
}

void
Player::draw(DrawingContext& context)
{
  if(!visible)
    return;

  // if Tux is above camera, draw little "air arrow" to show where he is x-wise
  if (Sector::current() && Sector::current()->camera && (get_bbox().p2.y - 16 < Sector::current()->camera->get_translation().y)) {
    float px = get_pos().x + (get_bbox().p2.x - get_bbox().p1.x - airarrow.get()->get_width()) / 2;
    float py = Sector::current()->camera->get_translation().y;
    py += std::min(((py - (get_bbox().p2.y + 16)) / 4), 16.0f);
    context.draw_surface(airarrow.get(), Vector(px, py), LAYER_HUD - 1);
  }

  std::string sa_prefix = "";
  std::string sa_postfix = "";

  if (player_status->bonus == GROWUP_BONUS)
    sa_prefix = "big";
  else if (player_status->bonus == FIRE_BONUS)
    sa_prefix = "fire";
  else if (player_status->bonus == ICE_BONUS)
    sa_prefix = "ice";
  else
    sa_prefix = "small";

  if(dir == LEFT)
    sa_postfix = "-left";
  else
    sa_postfix = "-right";

  /* Set Tux sprite action */
  if(dying) {
    sprite->set_action("gameover");
  }
  else if (growing) {
    sprite->set_action_continued("grow"+sa_postfix);
    // while growing, do not change action
    // do_duck() will take care of cancelling growing manually
    // update() will take care of cancelling when growing completed
  }
  else if (climbing) {
    sprite->set_action(sa_prefix+"-skid"+sa_postfix);
  }
  else if (backflipping) {
    sprite->set_action(sa_prefix+"-backflip"+sa_postfix);
  }
  else if (duck && is_big()) {
    sprite->set_action(sa_prefix+"-duck"+sa_postfix);
  }
  else if (skidding_timer.started() && !skidding_timer.check()) {
    sprite->set_action(sa_prefix+"-skid"+sa_postfix);
  }
  else if (kick_timer.started() && !kick_timer.check()) {
    sprite->set_action(sa_prefix+"-kick"+sa_postfix);
  }
  else if ((wants_buttjump || does_buttjump) && is_big()) {
    sprite->set_action(sa_prefix+"-buttjump"+sa_postfix);
  }
  else if (!on_ground()) {
    sprite->set_action(sa_prefix+"-jump"+sa_postfix);
  }
  else {
    if (fabsf(physic.get_velocity_x()) < 1.0f) {
      // Determine which idle stage we're at
      if (sprite->get_action().find("-stand-") == std::string::npos && sprite->get_action().find("-idle-") == std::string::npos) {
        idle_stage = 0;
        idle_timer.start(IDLE_TIME[idle_stage]/1000.0f);

        sprite->set_action_continued(sa_prefix+("-" + IDLE_STAGES[idle_stage])+sa_postfix);
      }
      else if (idle_timer.check() || (IDLE_TIME[idle_stage] == 0 && sprite->animation_done())) {
        idle_stage++;
        if (idle_stage >= IDLE_STAGE_COUNT)
            idle_stage = 1;

        idle_timer.start(IDLE_TIME[idle_stage]/1000.0f);

        if (IDLE_TIME[idle_stage] == 0)
          sprite->set_action(sa_prefix+("-" + IDLE_STAGES[idle_stage])+sa_postfix, 1);
        else
          sprite->set_action(sa_prefix+("-" + IDLE_STAGES[idle_stage])+sa_postfix);
      }
      else {
        sprite->set_action_continued(sa_prefix+("-" + IDLE_STAGES[idle_stage])+sa_postfix);
      }
    }
    else {
      sprite->set_action(sa_prefix+"-walk"+sa_postfix);
    }
  }


/*
  // Tux is holding something
  if ((grabbed_object != 0 && physic.get_velocity_y() == 0) ||
      (shooting_timer.get_timeleft() > 0 && !shooting_timer.check())) {
    if (duck) {
    } else {
    }
  }
*/

  /* Draw Tux */
  if (safe_timer.started() && size_t(game_time*40)%2)
    ;  // don't draw Tux
  else {
    sprite->draw(context, get_pos(), LAYER_OBJECTS + 1);
  }

}

void
Player::collision_tile(uint32_t tile_attributes)
{
  if(tile_attributes & Tile::HURTS)
    kill(false);

#ifdef SWIMMING
  if( swimming ){
    if( tile_attributes & Tile::WATER ){
      no_water = false;
    } else {
      swimming = false;
    }
  } else {
    if( tile_attributes & Tile::WATER ){
      swimming = true;
      no_water = false;
      sound_manager->play( "sounds/splash.ogg" );
    }
  }
#endif

  if(tile_attributes & Tile::ICE) {
    ice_this_frame = true;
    on_ice = true;
  }
}

void
Player::collision_solid(const CollisionHit& hit)
{
  if(hit.bottom) {
    if(physic.get_velocity_y() > 0)
      physic.set_velocity_y(0);

    on_ground_flag = true;
    floor_normal = hit.slope_normal;

    // Butt Jump landed    
    if (does_buttjump) {
      does_buttjump = false;
      physic.set_velocity_y(-300);
      on_ground_flag = false;
      Sector::current()->add_object(new Particles(
          Vector(get_bbox().p2.x, get_bbox().p2.y),
          270+20, 270+40,
          Vector(280, -260), Vector(0, 300), 3, Color(.4f, .4f, .4f), 3, .8f,
          LAYER_OBJECTS+1));
      Sector::current()->add_object(new Particles(
          Vector(get_bbox().p1.x, get_bbox().p2.y),
          90-40, 90-20,
          Vector(280, -260), Vector(0, 300), 3, Color(.4f, .4f, .4f), 3, .8f,
          LAYER_OBJECTS+1));
    }

  } else if(hit.top) {
    if(physic.get_velocity_y() < 0)
      physic.set_velocity_y(.2f);
  }

  if(hit.left || hit.right) {
    physic.set_velocity_x(0);
  }

  // crushed?
  if(hit.crush) {
    if(hit.left || hit.right) {
      kill(true);
    } else if(hit.top || hit.bottom) {
      kill(false);
    }
  }
}

HitResponse
Player::collision(GameObject& other, const CollisionHit& hit)
{
  Bullet* bullet = dynamic_cast<Bullet*> (&other);
  if(bullet) {
    return FORCE_MOVE;
  }

  if(hit.left || hit.right) {
    try_grab(); //grab objects right now, in update it will be too late
  }
#ifdef DEBUG
  assert(dynamic_cast<MovingObject*> (&other) != NULL);
#endif
  MovingObject* moving_object = static_cast<MovingObject*> (&other);
  if(moving_object->get_group() == COLGROUP_TOUCHABLE) {
    TriggerBase* trigger = dynamic_cast<TriggerBase*> (&other);
    if(trigger) {
      if(controller->pressed(Controller::UP))
        trigger->event(*this, TriggerBase::EVENT_ACTIVATE);
    }

    return FORCE_MOVE;
  }

  BadGuy* badguy = dynamic_cast<BadGuy*> (&other);
  if(badguy != NULL) {
    if(safe_timer.started() || invincible_timer.started())
      return FORCE_MOVE;

    return CONTINUE;
  }

  return CONTINUE;
}

void
Player::make_invincible()
{
  sound_manager->play("sounds/invincible_start.ogg");
  invincible_timer.start(TUX_INVINCIBLE_TIME);
  Sector::current()->play_music(HERRING_MUSIC);
}

/* Kill Player! */
void
Player::kill(bool completely)
{
  if(dying || deactivated)
    return;

  if(!completely && (safe_timer.started() || invincible_timer.started()))
    return;

  growing = false;

  sound_manager->play("sounds/hurt.wav");

  if (climbing) stop_climbing(*climbing);

  physic.set_velocity_x(0);

  if(!completely && is_big()) {
    if(player_status->bonus == FIRE_BONUS
        || player_status->bonus == ICE_BONUS) {
      safe_timer.start(TUX_SAFE_TIME);
      set_bonus(GROWUP_BONUS, true);
    } else if(player_status->bonus == GROWUP_BONUS) {
      safe_timer.start(TUX_SAFE_TIME /* + GROWING_TIME */);
      adjust_height(30.8f);
      duck = false;
      backflipping = false;
      set_bonus(NO_BONUS, true);
    } else if(player_status->bonus == NO_BONUS) {
      safe_timer.start(TUX_SAFE_TIME);
      adjust_height(30.8f);
      duck = false;
    }
  } else {

    // do not die when in edit mode
    if (edit_mode) {
      set_ghost_mode(true);
      return;
    }

    if (player_status->coins >= 25 && !GameSession::current()->get_reset_point_sectorname().empty())
    {
      for (int i = 0; i < 5; i++)
      {
        // the numbers: starting x, starting y, velocity y
        Sector::current()->add_object(new FallingCoin(get_pos() +
              Vector(systemRandom.rand(5), systemRandom.rand(-32,18)),
              systemRandom.rand(-100,100)));
      }
      player_status->coins -= std::max(player_status->coins/10, 25);
    }
    else
    {
      GameSession::current()->set_reset_point("", Vector());
    }
    physic.enable_gravity(true);
    physic.set_acceleration(0, 0);
    physic.set_velocity(0, -700);
    set_bonus(NO_BONUS, true);
    dying = true;
    dying_timer.start(3.0);
    set_group(COLGROUP_DISABLED);

    Sector::current()->effect->fade_out(3.0);
    sound_manager->stop_music(3.0);
  }
}

void
Player::move(const Vector& vector)
{
  set_pos(vector);

  // TODO: do we need the following? Seems irrelevant to moving the player
  if(is_big())
    set_size(31.8f, 63.8f);
  else
    set_size(31.8f, 31.8f);
  duck = false;
  last_ground_y = vector.y;
  if (climbing) stop_climbing(*climbing);

  physic.reset();
}

void
Player::check_bounds(Camera* camera)
{
  /* Keep tux in sector bounds: */
  if (get_pos().x < 0) {
    // Lock Tux to the size of the level, so that he doesn't fall off
    // the left side
    set_pos(Vector(0, get_pos().y));
  }

  if (get_bbox().get_right() > Sector::current()->get_width()) {
    // Lock Tux to the size of the level, so that he doesn't fall off
    // the right side
    set_pos(Vector(Sector::current()->get_width() - get_bbox().get_width(), get_pos().y));
  }

  /* fallen out of the level? */
  if ((get_pos().y > Sector::current()->get_height()) && (!ghost_mode)) {
    kill(true);
    return;
  }

  // can happen if back scrolling is disabled
  if(get_pos().x < camera->get_translation().x) {
    set_pos(Vector(camera->get_translation().x, get_pos().y));
  }
  if(get_pos().x >= camera->get_translation().x + SCREEN_WIDTH - bbox.get_width())
  {
    set_pos(Vector(
          camera->get_translation().x + SCREEN_WIDTH - bbox.get_width(),
          get_pos().y));
  }
}

void
Player::add_velocity(const Vector& velocity)
{
  physic.set_velocity(physic.get_velocity() + velocity);
}

void
Player::add_velocity(const Vector& velocity, const Vector& end_speed)
{
  if (end_speed.x > 0)
    physic.set_velocity_x(std::min(physic.get_velocity_x() + velocity.x, end_speed.x));
  if (end_speed.x < 0)
    physic.set_velocity_x(std::max(physic.get_velocity_x() + velocity.x, end_speed.x));
  if (end_speed.y > 0)
    physic.set_velocity_y(std::min(physic.get_velocity_y() + velocity.y, end_speed.y));
  if (end_speed.y < 0)
    physic.set_velocity_y(std::max(physic.get_velocity_y() + velocity.y, end_speed.y));
}

Vector 
Player::get_velocity()
{
  return physic.get_velocity();
}

void
Player::bounce(BadGuy& )
{
  if(controller->hold(Controller::JUMP))
    physic.set_velocity_y(-520);
  else
    physic.set_velocity_y(-300);
}

//Scripting Functions Below

void
Player::deactivate()
{
  if (deactivated)
    return;
  deactivated = true;
  physic.set_velocity_x(0);
  physic.set_velocity_y(0);
  physic.set_acceleration_x(0);
  physic.set_acceleration_y(0);
  if (climbing) stop_climbing(*climbing);
}

void
Player::activate()
{
  if (!deactivated)
    return;
  deactivated = false;
}

void Player::walk(float speed)
{
  physic.set_velocity_x(speed);
}

void
Player::set_ghost_mode(bool enable)
{
  if (ghost_mode == enable)
    return;

  if (climbing) stop_climbing(*climbing);

  if (enable) {
    ghost_mode = true;
    set_group(COLGROUP_DISABLED);
    physic.enable_gravity(false);
    log_debug << "You feel lightheaded. Use movement controls to float around, press ACTION to scare badguys." << std::endl;
  } else {
    ghost_mode = false;
    set_group(COLGROUP_MOVING);
    physic.enable_gravity(true);
    log_debug << "You feel solid again." << std::endl;
  }
}


void
Player::set_edit_mode(bool enable)
{
  edit_mode = enable;
}

void 
Player::start_climbing(Climbable& climbable)
{
  if (climbing == &climbable) return;

  climbing = &climbable;
  physic.enable_gravity(false);
  physic.set_velocity(0, 0);
  physic.set_acceleration(0, 0);
}

void 
Player::stop_climbing(Climbable& /*climbable*/)
{
  if (!climbing) return;

  climbing = 0;

  if (grabbed_object) {    
    grabbed_object->ungrab(*this, dir);
    grabbed_object = NULL;
  }

  physic.enable_gravity(true);
  physic.set_velocity(0, 0);
  physic.set_acceleration(0, 0);

  if ((controller->hold(Controller::JUMP)) || (controller->hold(Controller::UP))) {
    on_ground_flag = true;
    // TODO: This won't help. Why?
    do_jump(-300);
  }
}

void
Player::handle_input_climbing()
{
  if (!climbing) {
    log_warning << "handle_input_climbing called with climbing set to 0. Input handling skipped" << std::endl;
    return;
  }

  float vx = 0;
  float vy = 0;
  if (controller->hold(Controller::LEFT)) {
    dir = LEFT;
    vx -= MAX_CLIMB_XM;
  }
  if (controller->hold(Controller::RIGHT)) {
    dir = RIGHT;
    vx += MAX_CLIMB_XM;
  }
  if (controller->hold(Controller::UP)) {
    vy -= MAX_CLIMB_YM;
  }
  if (controller->hold(Controller::DOWN)) {
    vy += MAX_CLIMB_YM;
  }
  if (controller->hold(Controller::JUMP)) {
    if (can_jump) {
      stop_climbing(*climbing);
      return;
    }  
  } else {
    can_jump = true;
  }
  if (controller->hold(Controller::ACTION)) {
    stop_climbing(*climbing);
    return;
  }
  physic.set_velocity(vx, vy);
  physic.set_acceleration(0, 0);
}


