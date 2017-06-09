#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#include "mapping.h"
#include "routing.h"
#include "uthash.h"
#include "utlist.h"

#ifndef M_PI
#define M_PI 3.141592653589793238
#endif

world_t* routing_world;

typedef struct search_unit_t search_unit_t;
struct search_unit_t
{
	route_xy_t loc;
	float g;
	float f;
	int direction;

	search_unit_t* parent;

	UT_hash_handle hh;
};

#define sq(x) ((x)*(x))
#define MAX_F 99999999999999999.9

#define ROBOT_SHAPE_WINDOW 32
uint8_t robot_shapes[32][ROBOT_SHAPE_WINDOW][ROBOT_SHAPE_WINDOW];

int obstacle_limit = 0;
int unmapped_limit = 3;

static int check_hit(int x, int y, int direction)
{
	int num_unmapped = 0;
	int num_obstacles = 0;
	for(int chk_x=0; chk_x<ROBOT_SHAPE_WINDOW; chk_x++)
	{
		for(int chk_y=0; chk_y<ROBOT_SHAPE_WINDOW; chk_y++)
		{
			int pageidx_x, pageidx_y, pageoffs_x, pageoffs_y;
			page_coords_from_unit_coords(x-ROBOT_SHAPE_WINDOW/2+chk_x, y-ROBOT_SHAPE_WINDOW/2+chk_y, &pageidx_x, &pageidx_y, &pageoffs_x, &pageoffs_y);

			if(!routing_world->pages[pageidx_x][pageidx_y]) // out of bounds (not allocated) - give up instantly
			{
				return 1;
			}

			if(robot_shapes[direction][chk_x][chk_y])
			{
				if((routing_world->pages[pageidx_x][pageidx_y]->units[pageoffs_x][pageoffs_y].result & UNIT_WALL))
					num_obstacles++;


				if(!(routing_world->pages[pageidx_x][pageidx_y]->units[pageoffs_x][pageoffs_y].result & UNIT_MAPPED))
					num_unmapped++;
			}

		}
	}

	if(num_obstacles > obstacle_limit)
		return 1;

	if(num_unmapped > unmapped_limit)
		return 2;

	return 0;
}

static int test_robot_turn(int x, int y, float start, float end)
{
	int cw = 0;

	while(start >= 2.0*M_PI) start -= 2.0*M_PI;
	while(start < 0.0) start += 2.0*M_PI;

	while(end >= 2.0*M_PI) end -= 2.0*M_PI;
	while(end < 0.0) end += 2.0*M_PI;

	// Calc for CCW (positive angle):
	float da = end - start;
	while(da >= 2.0*M_PI) da -= 2.0*M_PI;
	while(da < 0.0) da += 2.0*M_PI;

	if(da > M_PI)
	{
		// CCW wasn't fine, turn CW
		cw = 1;
		da = start - end;
		while(da >= 2.0*M_PI) da -= 2.0*M_PI;
		while(da < 0.0) da += 2.0*M_PI;
	}

	int dir_cur = (start/(2.0*M_PI) * 32.0);
	int dir_end = (end/(2.0*M_PI) * 32.0);

//	printf("test_robot_turn()  start=%.4f  end=%.4f  da=%.4f,  cw=%d\n", start, end, da, cw);

	while(dir_cur != dir_end)
	{
//		printf("test_robot_turn(): dir_cur = %d, dir_end=%d\n", dir_cur, dir_end);

		if(check_hit(x, y, dir_cur))
			return 0;

		if(cw) dir_cur--; else dir_cur++;

		if(dir_cur < 0) dir_cur = 31;
		else if(dir_cur > 31) dir_cur = 0;

	}

	return 1;
}


static int line_of_sight(route_xy_t p1, route_xy_t p2)
{
	int dx = p2.x - p1.x;
	int dy = p2.y - p1.y;

	const float step = 500.0/MAP_UNIT_W;

	float len = sqrt(sq(dx) + sq(dy));

	float pos = 0.0;
	int terminate = 0;

	float ang = atan2(dy, dx);
	if(ang < 0.0) ang += 2.0*M_PI;
	int dir = (ang/(2.0*M_PI) * 32.0)+0.5;


//	printf("ang = %.4f  dir = %d \n", ang, dir);

	while(1)
	{
		int x = (cos(ang)*pos + (float)p1.x)+0.5;
		int y = (sin(ang)*pos + (float)p1.y)+0.5;

//		printf("check_hit(%d, %d, %d) = ", x, y, dir);

		if(check_hit(x, y, dir))
		{
//			printf("1 !\n");
			return 0;
		}
//		printf("0\n");

		if(terminate) break;
		pos += step;
		if(pos > len)
		{
			pos = len;
			terminate = 1;
		}
	}

	return 1;
}

#define SHAPE_PIXEL(shape, x, y) { robot_shapes[shape][(x)][(y)] = 1;}

int limits_x[ROBOT_SHAPE_WINDOW][2];

#define ABS(x) ((x >= 0) ? x : -x)
static void triangle_scanline(int x1, int y1, int x2, int y2)
{
	int sx, sy, dx1, dy1, dx2, dy2, x, y, m, n, k, cnt;

	sx = x2 - x1;
	sy = y2 - y1;

	if (sx > 0) dx1 = 1;
	else if (sx < 0) dx1 = -1;
	else dx1 = 0;

	if (sy > 0) dy1 = 1;
	else if (sy < 0) dy1 = -1;
	else dy1 = 0;

	m = ABS(sx);
	n = ABS(sy);
	dx2 = dx1;
	dy2 = 0;

	if (m < n)
	{
		m = ABS(sy);
		n = ABS(sx);
		dx2 = 0;
		dy2 = dy1;
	}

	x = x1; y = y1;
	cnt = m + 1;
	k = n / 2;

	while (cnt--)
	{
		if ((y >= 0) && (y < ROBOT_SHAPE_WINDOW))
		{
			if (x < limits_x[y][0]) limits_x[y][0] = x;
			if (x > limits_x[y][1]) limits_x[y][1] = x;
		}

		k += n;
		if (k < m)
		{
			x += dx2;
			y += dy2;
		}
		else
		{
			k -= m;
			x += dx1;
			y += dy1;
		}
	}
}

static void draw_triangle(int a_idx, int x0, int y0, int x1, int y1, int x2, int y2)
{
	int y;

	for (y = 0; y < ROBOT_SHAPE_WINDOW; y++)
	{
		limits_x[y][0] = 999999; // min X
		limits_x[y][1] = -999999; // max X
	}

	triangle_scanline(x0, y0, x1, y1);
	triangle_scanline(x1, y1, x2, y2);
	triangle_scanline(x2, y2, x0, y0);

	for (y = 0; y < ROBOT_SHAPE_WINDOW; y++)
	{
		if (limits_x[y][1] >= limits_x[y][0])
		{
			int x = limits_x[y][0];
			int len = 1 + limits_x[y][1] - limits_x[y][0];

			// Horizontal line.
			while (len--)
			{
				SHAPE_PIXEL(a_idx, x, y);
				x++;
			}
		}
	}
}

#define TODEG(x) ((360.0*x)/(2.0*M_PI))

int tight_shapes = 0;

static void draw_robot_shape(int a_idx, float ang)
{
	float o_x = (ROBOT_SHAPE_WINDOW/2.0)*(float)MAP_UNIT_W;
	float o_y = (ROBOT_SHAPE_WINDOW/2.0)*(float)MAP_UNIT_W;

	// Robot size plus margin
//	float robot_xs = (524.0 + 20.0);
//	float robot_ys = (480.0 + 20.0);

	float robot_xs, robot_ys;

	if(tight_shapes)
	{
		robot_xs = (524.0 + 10.0);
		robot_ys = (480.0 + 10.0);
	}
	else
	{
		robot_xs = (524.0 + 100.0);
		robot_ys = (480.0 + 200.0);
	}

	float middle_xoffs = -120.0; // from o_x, o_y to the robot middle point.
	float middle_yoffs = -0.0;

/*

	Y                         / positive angle
	^                       /
	|                     /
	|                   /
	+----> X            -------------> zero angle

	Corner numbers:

	1-----------------------2
	|                       |
	|                       |
	|                       |
	|                 O     |  --->
	|                       |
	|                       |
	|                       |
	4-----------------------3

	Vector lengths O->1, O->2, O->3, O->4 are called a,b,c,d, respectively.
	Angles of those vectors relative to O are called ang1, ang2, ang3, ang4.

*/	
	// Basic Pythagoras thingie
	float a = sqrt(sq(0.5*robot_xs - middle_xoffs) + sq(0.5*robot_ys + middle_yoffs));
	float b = sqrt(sq(0.5*robot_xs + middle_xoffs) + sq(0.5*robot_ys + middle_yoffs));
	float c = sqrt(sq(0.5*robot_xs + middle_xoffs) + sq(0.5*robot_ys - middle_yoffs));
	float d = sqrt(sq(0.5*robot_xs - middle_xoffs) + sq(0.5*robot_ys - middle_yoffs));

//	printf("a=%.1f b=%.1f c=%.1f d=%.1f\n", a,b,c,d);

	// The angles:
	float ang1 = M_PI - asin((0.5*robot_ys)/a);
	float ang2 = asin((0.5*robot_ys)/b);
	float ang3 = -1*asin((0.5*robot_ys)/c);
	float ang4 = -1*(M_PI - asin((0.5*robot_ys)/d));

//	printf("ang1=%.2f ang2=%.2f ang3=%.2f ang4=%.2f\n", ang1,ang2,ang3,ang4);
//	printf("(ang1=%.2f ang2=%.2f ang3=%.2f ang4=%.2f)\n", TODEG(ang1),TODEG(ang2),TODEG(ang3),TODEG(ang4));

//	printf("adj ang = %.2f\n", ang);

	// Turn the whole robot:
	ang1 += ang;
	ang2 += ang;
	ang3 += ang;
	ang4 += ang;

//	printf("ang1=%.2f ang2=%.2f ang3=%.2f ang4=%.2f\n", ang1,ang2,ang3,ang4);

	float x1 = cos(ang1)*a + o_x;
	float y1 = sin(ang1)*a + o_y;
	float x2 = cos(ang2)*b + o_x;
	float y2 = sin(ang2)*b + o_y;
	float x3 = cos(ang3)*c + o_x;
	float y3 = sin(ang3)*c + o_y;
	float x4 = cos(ang4)*d + o_x;
	float y4 = sin(ang4)*d + o_y;

//	printf("x1 = %4.0f  y1 = %4.0f\n", x1, y1);
//	printf("x2 = %4.0f  y2 = %4.0f\n", x2, y2);
//	printf("x3 = %4.0f  y3 = %4.0f\n", x3, y3);
//	printf("x4 = %4.0f  y4 = %4.0f\n", x4, y4);

	// "Draw" the robot in two triangles:

	// First, triangle 1-2-3
	draw_triangle(a_idx, x1/(float)MAP_UNIT_W, y1/(float)MAP_UNIT_W, x2/(float)MAP_UNIT_W, y2/(float)MAP_UNIT_W, x3/(float)MAP_UNIT_W, y3/(float)MAP_UNIT_W);
	// Second, triangle 1-3-4
	draw_triangle(a_idx, x1/(float)MAP_UNIT_W, y1/(float)MAP_UNIT_W, x3/(float)MAP_UNIT_W, y3/(float)MAP_UNIT_W, x4/(float)MAP_UNIT_W, y4/(float)MAP_UNIT_W);
}


static void gen_robot_shapes()
{
	// Generate lookup tables showing the shape of robot in mapping unit matrices in different orientations.
	// These are used in mapping to test whether the (x,y) coords result in some part of a robot hitting a wall.

	for(int a=0; a<32; a++)
	{
		draw_robot_shape(a, ((float)a*2.0*M_PI)/32.0);

/*		printf("a = %d\n", a);

		for(int y = 0; y < ROBOT_SHAPE_WINDOW; y++)
		{
			for(int x = 0; x < ROBOT_SHAPE_WINDOW; x++)
			{
				printf(robot_shapes[a][x][y]?"# ":"  ");
			}
			printf("\n");
		}
*/
	}

}

static void normal_search_mode()
{
	obstacle_limit = 0;
	unmapped_limit = 3;
	tight_shapes = 0;
	gen_robot_shapes();	
}

static void tight_search_mode()
{
	obstacle_limit = 1;
	unmapped_limit = 4;
	tight_shapes = 1;
	gen_robot_shapes();	
}

search_unit_t* closed_set = NULL;
search_unit_t* open_set = NULL;

void clear_route(route_unit_t **route)
{
	route_unit_t *elt, *tmp;
	DL_FOREACH_SAFE(*route,elt,tmp)
	{
		DL_DELETE(*route,elt);
		free(elt);
	}
	*route = NULL;
}

static int search(route_unit_t **route, float start_ang, int start_x_mm, int start_y_mm, int end_x_mm, int end_y_mm)
{
	clear_route(route);

	int s_x, s_y, e_x, e_y;
	unit_coords(start_x_mm, start_y_mm, &s_x, &s_y);
	unit_coords(end_x_mm, end_y_mm, &e_x, &e_y);

	while(start_ang >= 2.0*M_PI) start_ang -= 2.0*M_PI;
	while(start_ang < 0.0) start_ang += 2.0*M_PI;
	int start_dir = (start_ang/(2.0*M_PI) * 32.0)+0.5;

	printf("Start %d,%d,  end %d,%d  start_ang=%f  start_dir=%d\n", s_x, s_y, e_x, e_y, start_ang, start_dir);


	search_unit_t* p_start = (search_unit_t*) malloc(sizeof(search_unit_t));
	memset(p_start, 0, sizeof(search_unit_t));

	p_start->loc.x = s_x;
	p_start->loc.y = s_y;
	p_start->direction = start_dir;
	p_start->parent = NULL;
	// g = 0
	p_start->f = sqrt((float)(sq(e_x-s_x) + sq(e_y-s_y)));

	HASH_ADD(hh, open_set, loc,sizeof(route_xy_t), p_start);

	int cnt = 0;

	while(HASH_CNT(hh, open_set) > 0)
	{
		cnt++;

		if(cnt > 200000)
		{
			printf("Giving up at cnt = %d\n", cnt);
			return 3;
		}

		// Find the lowest f score from open_set.
		search_unit_t* p_cur = NULL;
		float lowest_f = 2.0*MAX_F;
		for(search_unit_t* p_iter = open_set; p_iter != NULL; p_iter=p_iter->hh.next)
		{
			if(p_iter->f < lowest_f)
			{
				lowest_f = p_iter->f;
				p_cur = p_iter;
			}
		}

		if(p_cur == NULL)
		{
			printf("search() error: open set empty while finding lowest f score.\n");
			return 55;
		}

		if(p_cur->loc.x == e_x && p_cur->loc.y == e_y)
		{

			printf("Solution found, cnt = %d\n", cnt);

			// solution found.

			// Reconstruct the path

			search_unit_t* p_recon = p_cur;
			while( (p_recon = p_recon->parent) )
			{
				route_unit_t* point = malloc(sizeof(route_unit_t));
				point->loc.x = p_recon->loc.x; point->loc.y = p_recon->loc.y;
				point->backmode = 0;
				DL_PREPEND(*route, point);
			}

			route_unit_t *rt = *route;
			while(1)
			{
				if(rt->next && rt->next->next)
				{
					if(line_of_sight(rt->loc, rt->next->next->loc))
					{
//						printf("Deleting.\n");
						route_unit_t *tmp = rt->next;
						DL_DELETE(*route, tmp);
						free(tmp);
					}
					else
						rt = rt->next;
				}
				else
					break;
			}

			// Remove the first, because it's the starting point.
			route_unit_t *tm = *route;
			DL_DELETE(*route, tm);
			free(tm);

			// Free all memory.
			search_unit_t *p_del, *p_tmp;
			HASH_ITER(hh, closed_set, p_del, p_tmp)
			{
				HASH_DEL(closed_set, p_del);
				free(p_del);
			}
			HASH_ITER(hh, open_set, p_del, p_tmp)
			{
				HASH_DEL(open_set, p_del);
				free(p_del);
			}

			return 0;
		}

		// move from open to closed:
		HASH_DELETE(hh, open_set, p_cur);
		HASH_ADD(hh, closed_set, loc,sizeof(route_xy_t), p_cur);

		// For each neighbor
		for(int xx=-1; xx<=1; xx++)
		{
			for(int yy=-1; yy<=1; yy++)
			{
				search_unit_t* found;
				float new_g;
				float new_g_from_parent;
				if(xx == 0 && yy == 0) continue;

				search_unit_t* p_neigh;
				route_xy_t neigh_loc = {p_cur->loc.x + xx, p_cur->loc.y + yy};

				// Check if it's out-of-allowed area here:


				HASH_FIND(hh, closed_set, &neigh_loc,sizeof(route_xy_t), found);
				if(found)
					continue; // ignore neighbor that's in closed_set.


				// gscore for the neigbor: distance from the start to neighbor
				// is current unit's g score plus distance to the neighbor.
				new_g = p_cur->g + sqrt((float)(sq(p_cur->loc.x-neigh_loc.x) + sq(p_cur->loc.y-neigh_loc.y)));

				// for theta*:
				if(p_cur->parent)
					new_g_from_parent = p_cur->parent->g + sqrt((float)(sq(p_cur->parent->loc.x-neigh_loc.x) + sq(p_cur->parent->loc.y-neigh_loc.y)));


				HASH_FIND(hh, open_set, &neigh_loc,sizeof(route_xy_t), p_neigh);


				int direction_from_cur_parent = -1;
				int direction_from_neigh_parent = -1;

				if(p_cur->parent)  // Use this if possible
				{
					int dir_dx = neigh_loc.x - p_cur->parent->loc.x;
					int dir_dy = neigh_loc.y - p_cur->parent->loc.y;
					float ang = atan2(dir_dy, dir_dx);
					if(ang < 0.0) ang += 2.0*M_PI;
					int dir_parent = ang/(2.0*M_PI) * 32.0;
					direction_from_cur_parent = dir_parent;
				}

				if(p_neigh && p_neigh->parent)  // secondary
				{
					int dir_dx = p_neigh->parent->loc.x - neigh_loc.x;
					int dir_dy = p_neigh->parent->loc.y - neigh_loc.y;
					float ang = atan2(dir_dy, dir_dx);
					if(ang < 0.0) ang += 2.0*M_PI;
					int dir_parent = ang/(2.0*M_PI) * 32.0;
					direction_from_neigh_parent = dir_parent;
				}

				int direction = 0;
				if(direction_from_cur_parent < 0 && direction_from_neigh_parent < 0)  // if the previous two don't work out.
				{
					if(xx==1 && yy==1)        direction = 1*4; 
					else if(xx==0 && yy==1)   direction = 2*4; 
					else if(xx==-1 && yy==1)  direction = 3*4; 
					else if(xx==-1 && yy==0)  direction = 4*4; 
					else if(xx==-1 && yy==-1) direction = 5*4; 
					else if(xx==0 && yy==-1)  direction = 6*4; 
					else if(xx==1 && yy==-1)  direction = 7*4; 
				}
				else
				{
					if(direction_from_cur_parent >= 0)
						direction = direction_from_cur_parent;
					else
						direction = direction_from_neigh_parent;
				}


				// If this is the first neighbor search, test if the robot can turn:
				if(cnt == 1)
				{
					if(!test_robot_turn(p_cur->loc.x, p_cur->loc.y, start_ang, ((float)direction/32.0)*2.0*M_PI*direction))
					{
						printf("Robot cannot turn to direction %d\n", direction);
						continue;
					}
				}

				if(check_hit(neigh_loc.x, neigh_loc.y, direction))
				{

					if(direction_from_neigh_parent != -1)
					{
						// try another thing before giving up:
						direction = direction_from_neigh_parent;

						if(check_hit(neigh_loc.x, neigh_loc.y, direction))
							continue;
					}
					else
						continue;

				}


				if(!p_neigh)
				{
					p_neigh = (search_unit_t*) malloc(sizeof(search_unit_t));
					memset(p_neigh, 0, sizeof(search_unit_t));
					p_neigh->loc.x = neigh_loc.x; p_neigh->loc.y = neigh_loc.y;
					HASH_ADD(hh, open_set, loc,sizeof(route_xy_t), p_neigh);

					p_neigh->direction = direction;
					p_neigh->parent = p_cur;
					p_neigh->g = new_g;
					p_neigh->f = new_g + sqrt((float)(sq(e_x-neigh_loc.x) + sq(e_y-neigh_loc.y)));

				}
				else
				{
					if(p_cur->parent && line_of_sight(p_cur->parent->loc, p_neigh->loc)) // Theta* style near-optimum (probably shortest) path
					{
						if(new_g_from_parent < p_neigh->g)
						{
							p_neigh->direction = direction;
							p_neigh->parent = p_cur->parent;
							p_neigh->g = new_g_from_parent;
							p_neigh->f = new_g_from_parent + sqrt((float)(sq(e_x-neigh_loc.x) + sq(e_y-neigh_loc.y)));
						}
					}
					else if(new_g < p_neigh->g)  // A* style path shorter than before.
					{
						p_neigh->direction = direction;
						p_neigh->parent = p_cur;
						p_neigh->g = new_g;
						p_neigh->f = new_g + sqrt((float)(sq(e_x-neigh_loc.x) + sq(e_y-neigh_loc.y)));
					}
				}

			}


		}		
	}

	search_unit_t *p_del, *p_tmp;
	HASH_ITER(hh, closed_set, p_del, p_tmp)
	{
		HASH_DELETE(hh, closed_set, p_del);
		free(p_del);
	}
	HASH_ITER(hh, open_set, p_del, p_tmp)
	{
		HASH_DELETE(hh, open_set, p_del);
		free(p_del);
	}
	
	printf("Route not found, cnt = %d\n", cnt);

	if(cnt < 200)
	{
		return 1;
	}

	return 2;
	// Failure.

}


int search2(route_unit_t **route, float start_ang, int start_x_mm, int start_y_mm, int end_x_mm, int end_y_mm)
{

#define SRCH_NUM_A 11
	static const int a_s[SRCH_NUM_A] = 
	{	0,	-12,	12,	-24,	24,	-36,	36,	-48,	48,	-60,	60	};


#define SRCH_NUM_BACK 8
	static const int b_s[SRCH_NUM_BACK] = 
	{	-80,	-120,	-160,	-200,	-280,	-360,	-440,	-520	};


	// If going forward doesn't work out from the beginning, try backing off slightly.

	wdbg("within search2(): begin");

	int ret = search(route, start_ang, start_x_mm, start_y_mm, end_x_mm, end_y_mm);
	wdbg("within search2(): after first search()");


	if(ret == 0)
		return 0;

	if(ret == 1)
	{
		printf("Search fails in the start - trying to back off.\n");

		for(int a_idx = 0; a_idx < SRCH_NUM_A; a_idx++)
		{
			for(int back_idx = 0; back_idx < SRCH_NUM_BACK; back_idx++)
			{
				float new_ang = start_ang + (2.0*M_PI*(float)a_s[a_idx]/360.0);
				while(new_ang >= 2.0*M_PI) new_ang -= 2.0*M_PI;
				while(new_ang < 0.0) new_ang += 2.0*M_PI;
				int new_x = start_x_mm + cos(new_ang)*b_s[back_idx];
				int new_y = start_y_mm + sin(new_ang)*b_s[back_idx];

				int dir = (new_ang/(2.0*M_PI) * 32.0)+0.5;

				int new_x_units, new_y_units;
				unit_coords(new_x, new_y, &new_x_units, &new_y_units);

				printf("Back off ang=%.2f deg, mm = %d  -> new start = (%d, %d) --> ", TODEG(new_ang), b_s[back_idx], new_x_units, new_y_units);

				if(check_hit(new_x_units, new_y_units, dir))
				{
					printf("backing off hits the wall.\n");
				}
				else
				{
					int ret = search(route, new_ang, new_x, new_y, end_x_mm, end_y_mm);
					if(ret == 0)
					{
						printf("Search succeeded, stopping back-off search.\n");

						route_unit_t* point = malloc(sizeof(route_unit_t));
						point->loc.x = new_x_units; point->loc.y = new_y_units;
						point->backmode = 1;			
						DL_PREPEND(*route, point);

						return 0;
					}
					else if(ret > 1)
					{
						printf("Search failed later than in the beginning, stopping back-off search.\n");
						return 2;
					}
				}

			}
		}
		return 1;
	}

	return 3;

}


int search_route(world_t *w, route_unit_t **route, float start_ang, int start_x_mm, int start_y_mm, int end_x_mm, int end_y_mm)
{
	routing_world = w;
	normal_search_mode();
	printf("Searching with normal limits...\n");
	wdbg("within search_route: before anything");

	if(search2(route, start_ang, start_x_mm, start_y_mm, end_x_mm, end_y_mm))
	{
		wdbg("within search_route: after failed search2");
		printf("Search failed - retrying with tighter limits.\n");
		tight_search_mode();
		if(search2(route, start_ang, start_x_mm, start_y_mm, end_x_mm, end_y_mm))
		{
			wdbg("within search_route: after failed tight search2");

			printf("There is no route.\n");
			return 1;
		}
		wdbg("within search_route: after the tight if(search2");

	}

	wdbg("within search_route: after the first if(search2");
	return 0;

}
