#include "app_common.h"

inline bool operator==(CameraState& lhs, CameraState& rhs)
{
	b32 res = (lhs.world_center == rhs.world_center && lhs.scale == rhs.scale && lhs.sub_world_center == rhs.sub_world_center);
	return res;
}


struct IHM
{
	//input handling memory
	uint64 prev_update_tick;
	uint64 update_tick_time;
	b32 paused;
	vec2i prev_mouse_pos;
	b32 in_panning_mode;
	//------------------------
};

void init_input_handler(PL* pl, AppMemory* gm)
{
	gm->input_handling_memory = MARENA_PUSH(&pl->memory.main_arena, sizeof(IHM), "Input Handling memory struct");

	IHM* ihm = (IHM*)gm->input_handling_memory;

	ihm->prev_update_tick = pl->time.current_millis;
	ihm->update_tick_time = 100;
	ihm->prev_mouse_pos = { 0,0 };
}

static void update_input_handler(PL* pl, AppMemory* gm)
{
	IHM* ihm = (IHM*)gm->input_handling_memory;
	CameraState prev_cm_state = gm->cm;	//used to check if the camera state was changed. 

	if (pl->input.keys[PL_KEY::SPACE].pressed)
	{
		ihm->paused = !ihm->paused;
	}

	if (pl->input.mouse.middle.pressed)	//in panning mode
	{
		ASSERT(ihm->in_panning_mode != TRUE);
		ihm->in_panning_mode = TRUE;
		ihm->prev_mouse_pos = { pl->input.mouse.position_x,pl->input.mouse.position_y };
	}

	if (pl->input.mouse.middle.released)
	{
		ihm->in_panning_mode = FALSE;
	}

	if (ihm->in_panning_mode)
	{
		if (!pl->input.mouse.is_in_window)
		{
			ihm->in_panning_mode = FALSE;
		}
		else
		{
			Vec2<f32> delta_mouse = { (f32)(pl->input.mouse.position_x - ihm->prev_mouse_pos.x),(f32)(pl->input.mouse.position_y - ihm->prev_mouse_pos.y) };
			delta_mouse = delta_mouse * -1;		//inverting so it's 'pull to scroll'
			delta_mouse *= (f32)gm->cm.scale;	//scaled to world position delta vector (precision loss from f64 to f32 shouldn't matter too much on deltas.)
			//now adding delta mouse to camera position
			vec2f delta_sub_world_pos;

			WorldPos delta_world_pos = { (int64)delta_mouse.x, (int64)delta_mouse.y };
			delta_sub_world_pos = { delta_mouse.x - (int32)delta_world_pos.x,delta_mouse.y - (int32)delta_world_pos.y };

			gm->cm.world_center += delta_world_pos;
			gm->cm.sub_world_center += delta_sub_world_pos;
			if (gm->cm.sub_world_center.x < .0f)
			{
				gm->cm.world_center.x--;
				gm->cm.sub_world_center.x = 1.0f + gm->cm.sub_world_center.x;	//wrapping down back to [0.0 to 1.0)
			}
			if (gm->cm.sub_world_center.x >= 1.0f)
			{
				gm->cm.world_center.x++;
				gm->cm.sub_world_center.x = gm->cm.sub_world_center.x - 1.0f;	//wrapping down back to [0.0 to 1.0)
			}
			if (gm->cm.sub_world_center.y < .0f)
			{
				gm->cm.world_center.y--;
				gm->cm.sub_world_center.y = 1.0f + gm->cm.sub_world_center.y;	//wrapping down back to [0.0 to 1.0)
			}
			if (gm->cm.sub_world_center.y >= 1.0f)
			{
				gm->cm.world_center.y++;
				gm->cm.sub_world_center.y = gm->cm.sub_world_center.y - 1.0f;	//wrapping down back to [0.0 to 1.0)
			}


			ihm->prev_mouse_pos.x = pl->input.mouse.position_x;
			ihm->prev_mouse_pos.y = pl->input.mouse.position_y;

		}
	}

	if (pl->input.mouse.scroll_delta != 0)
	{
		//TODO: Add zoom towards specific worldpos. 

		//Zoom is Fantastic! 
		f64 zoom_factor = 10;
		if (pl->input.mouse.scroll_delta > 0)
		{
			zoom_factor *= (gm->cm.scale - 0.05);	//closer it is, less the zoom factor becomes.
		}
		else
		{
			zoom_factor *= (1.0 - gm->cm.scale);
		}
		f64 scroll_delta = -pl->input.mouse.scroll_delta;
		scroll_delta *= pl->time.fdelta_seconds * zoom_factor;
		gm->cm.scale += scroll_delta;
		gm->cm.scale = clamp(gm->cm.scale, 0.05, 1.0);

	}


	if (ihm->paused)
	{
		if (pl->input.keys[PL_KEY::LEFT_SHIFT].down)
		{
			if (pl->input.mouse.left.down)	//adding cell
			{
				//Set state of cell.

				WorldPos screen_coords = { (int64)pl->input.mouse.position_x - (pl->window.width / 2),(int64)pl->input.mouse.position_y - (pl->window.height / 2) };
				screen_coords = screen_to_world(screen_coords, gm->cm);

				uint32 slot = hash_pos(screen_coords, gm->table_size);
				b32 state = lookup_cell(gm->active_table, slot, screen_coords);
				//add only if state is false (doesn't exist in table). 
				if (!state)
				{
					//pl_debug_print("Added: [%i, %i]\n", screen_coords.x, screen_coords.y);
					append_new_node(gm->active_table, slot, screen_coords);
				}
			}
			else if (pl->input.mouse.right.down)	//removing cell
			{
				WorldPos screen_coords = { (int64)pl->input.mouse.position_x - (pl->window.width / 2),(int64)pl->input.mouse.position_y - (pl->window.height / 2) };
				screen_coords = screen_to_world(screen_coords, gm->cm);

				uint32 slot = hash_pos(screen_coords, gm->table_size);

				LiveCellNode* front = gm->active_table->table_front[slot];
				if (front == 0)
				{
					goto ABORT_MULTI_CELL_REMOVAL;
				}
				if (front->pos.x == screen_coords.x && front->pos.y == screen_coords.y)
				{
					gm->active_table->table_front[slot] = front->next;
				}
				else
				{
					LiveCellNode* prev = front;
					LiveCellNode* next = front->next;
					if (next == 0)
					{
						goto ABORT_MULTI_CELL_REMOVAL;	//Cell doesn't exist.
					}
					else
					{
						while ((next->pos.x != screen_coords.x && next->pos.y != screen_coords.y))
						{
							prev = next;
							next = next->next;
							if (next == 0)
							{
								goto ABORT_MULTI_CELL_REMOVAL;	//Cell doesn't exist. End of list. 
							}
						}
						prev->next = next->next;
					}

				}
				gm->cell_removed_from_table = TRUE;
			ABORT_MULTI_CELL_REMOVAL:;
			}
		}
		else
		{
			if (pl->input.mouse.left.pressed)	//adding cell
			{
				//Set state of cell.

				WorldPos screen_coords = { (int64)pl->input.mouse.position_x - (pl->window.width / 2),(int64)pl->input.mouse.position_y - (pl->window.height / 2) };
				screen_coords = screen_to_world(screen_coords, gm->cm);

				uint32 slot = hash_pos(screen_coords, gm->table_size);
				b32 state = lookup_cell(gm->active_table, slot, screen_coords);
				//add only if state is false (doesn't exist in table). 
				if (!state)
				{
					//pl_debug_print("Added: [%i, %i]\n", screen_coords.x, screen_coords.y);
					append_new_node(gm->active_table, slot, screen_coords);
				}
			}
			else if (pl->input.mouse.right.pressed)	//removing cell
			{
				WorldPos screen_coords = { (int64)pl->input.mouse.position_x - (pl->window.width / 2),(int64)pl->input.mouse.position_y - (pl->window.height / 2) };
				screen_coords = screen_to_world(screen_coords, gm->cm);

				uint32 slot = hash_pos(screen_coords, gm->table_size);

				LiveCellNode* front = gm->active_table->table_front[slot];
				if (front == 0)
				{
					goto ABORT_CELL_REMOVAL;
				}
				if (front->pos.x == screen_coords.x && front->pos.y == screen_coords.y)
				{
					gm->active_table->table_front[slot] = front->next;
				}
				else
				{
					LiveCellNode* prev = front;
					LiveCellNode* next = front->next;
					if (next == 0)
					{
						goto ABORT_CELL_REMOVAL;	//Cell doesn't exist.
					}
					else
					{
						while ((next->pos.x != screen_coords.x && next->pos.y != screen_coords.y))
						{
							prev = next;
							next = next->next;
							if (next == 0)
							{
								goto ABORT_CELL_REMOVAL;	//Cell doesn't exist. End of list. 
							}
						}
						prev->next = next->next;
					}
				}
				gm->cell_removed_from_table = TRUE;
			ABORT_CELL_REMOVAL:;
			}
		}

	}

	if (!ihm->paused && (pl->time.current_millis >= ihm->prev_update_tick + ihm->update_tick_time))
	{
		//update grid
		ihm->prev_update_tick = pl->time.current_millis;
		gm->update_grid_flag = TRUE;
	}


	if (!(prev_cm_state == gm->cm))
	{
		gm->camera_changed = TRUE;
	}
}

void shutdown_input_handler(PL* pl, AppMemory* gm)
{
	MARENA_POP(&pl->memory.main_arena, sizeof(IHM), "Input Handling memory struct");
}

void handle_input(PL* pl, AppMemory* gm)
{
	update_input_handler(pl, gm);
}