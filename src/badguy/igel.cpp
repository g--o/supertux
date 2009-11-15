//  SuperTux - Badguy "Igel"
//  Copyright (C) 2006 Christoph Sommer <christoph.sommer@2006.expires.deltadevelopment.de>
//
//  This program is free software: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation, either version 3 of the License, or
//  (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program.  If not, see <http://www.gnu.org/licenses/>.

#include <config.h>

#include "badguy/igel.hpp"
#include "object/block.hpp"
#include "supertux/sector.hpp"
#include "object/bullet.hpp"

#include "lisp/writer.hpp"
#include "supertux/object_factory.hpp"

namespace {
  const float WALKSPEED = 80; /**< speed at which we walk around */
  const float TURN_RECOVER_TIME = 0.5; /**< seconds before we will again turn around when shot at */
  const float RANGE_OF_VISION = 256; /**< range in px at which we can see bullets */
}

Igel::Igel(const lisp::Lisp& reader)
  : WalkingBadguy(reader, "images/creatures/igel/igel.sprite", "walking-left", "walking-right")
{
  walk_speed = WALKSPEED;
  max_drop_height = 16;
}

Igel::Igel(const Vector& pos, Direction d)
  : WalkingBadguy(pos, d, "images/creatures/igel/igel.sprite", "walking-left", "walking-right")
{
  walk_speed = WALKSPEED;
  max_drop_height = 16;
}

void
Igel::write(lisp::Writer& writer)
{
  writer.start_list("igel");
  WalkingBadguy::write(writer);
  writer.end_list("igel");
}

void
Igel::be_normal()
{
  initialize();
}

void
Igel::turn_around()
{
  WalkingBadguy::turn_around();
  turn_recover_timer.start(TURN_RECOVER_TIME);
}

bool
Igel::can_see(const MovingObject& o)
{
  Rect mb = get_bbox();
  Rect ob = o.get_bbox();

  bool inReach_left = ((ob.p2.x < mb.p1.x) && (ob.p2.x >= mb.p1.x-((dir == LEFT) ? RANGE_OF_VISION : 0)));
  bool inReach_right = ((ob.p1.x > mb.p2.x) && (ob.p1.x <= mb.p2.x+((dir == RIGHT) ? RANGE_OF_VISION : 0)));
  bool inReach_top = (ob.p2.y >= mb.p1.y);
  bool inReach_bottom = (ob.p1.y <= mb.p2.y);

  return ((inReach_left || inReach_right) && inReach_top && inReach_bottom);
}

void
Igel::active_update(float elapsed_time)
{
  bool wants_to_flee = false;

  // check if we see a fire bullet
  Sector* sector = Sector::current();
  for (Sector::GameObjects::iterator i = sector->gameobjects.begin(); i != sector->gameobjects.end(); ++i) {
    Bullet* bullet = dynamic_cast<Bullet*>(*i);
    if (!bullet) continue;
    if (bullet->get_type() != FIRE_BONUS) continue;
    if (can_see(*bullet)) wants_to_flee = true;
  }

  // if we flee, handle this ourselves
  if (wants_to_flee && (!turn_recover_timer.started())) {
    turn_around();
    BadGuy::active_update(elapsed_time);
    return;
  }

  // else adhere to default behaviour
  WalkingBadguy::active_update(elapsed_time);
}

HitResponse
Igel::collision_bullet(Bullet& bullet, const CollisionHit& hit)
{
  // default reaction if hit on front side
  if (((dir == LEFT) && hit.left) || ((dir == RIGHT) && hit.right)) {
    return BadGuy::collision_bullet(bullet, hit);
  }

  // else make bullet ricochet and ignore the hit
  bullet.ricochet(*this, hit);
  return FORCE_MOVE;
}

bool
Igel::collision_squished(GameObject& )
{
  // this will hurt
  return false;
}

IMPLEMENT_FACTORY(Igel, "igel");
