#include "app_common.h"
#include "ATProfiler/atp.h"



//Grid Processor Memory
struct GPM
{
	MArena gpm_arena;
	MArena gpm_temp_arena; 

	//A double buffer that stores the hash table and all the live cells. Refer active_table pointer for the 'active table'. 
	Hashtable table1;
	Hashtable table2;
};
static void process_cell(LiveCellNode* cell, AppMemory* gm, Hashtable* next_table, MSlice<WorldPos, uint32>& new_cells_tested);

static void update_cellgrid(PL* pl, AppMemory* gm)
{
	//---d--
	max_hash_depth = 0;
	//---d--


	GPM* gpm = (GPM*)gm->grid_processor_memory;

	Hashtable* next_table;
	if (gm->active_table == &gpm->table1)
	{
		next_table = &gpm->table2;
	}
	else
	{
		next_table = &gpm->table1;
	}

	MSlice<WorldPos, uint32> new_cells_tested;	//used to keep track of all the neighbors of lives cells that have been already processed
	new_cells_tested.init(&gm->active_table->arena, "new cells process queue");

	if (!gm->cell_removed_from_table)
	{
		//Just iterating through node stack instead of table. Avoids going through all empty slots in table. 
		LiveCellNode* it = gm->active_table->node_list.front;
		for (uint32 i = 0; i < gm->active_table->node_list.size; i++)
		{
			//pl_debug_print("Active Cells:%i\n", gm->active_table->node_list.size);
			process_cell(it, gm, next_table, new_cells_tested);
			it++;
		}
	}
	else
	{
		// Iterating through table and processing all nodes in table (because for cell removal, cell is removed from table but not from stack.)
		LiveCellNode** prev_table_front = gm->active_table->table_front;
		for (uint32 table_pos = 0; table_pos < gm->table_size; table_pos++)
		{
			if (prev_table_front[table_pos] != 0)
			{
				LiveCellNode* list_node = prev_table_front[table_pos];
				do
				{
					process_cell(list_node, gm, next_table, new_cells_tested);
					list_node = list_node->next;
				} while (list_node != 0);
			}
		}
		gm->cell_removed_from_table = FALSE;
	}
	//resetting top of the arena to just having the hashtable. 
	new_cells_tested.clear(&gm->active_table->arena);
	gm->active_table->node_list.clear(&gm->active_table->arena);
	gm->active_table->node_list.front = 0;
	gm->active_table->node_list.front = (LiveCellNode*)MARENA_TOP(&gm->active_table->arena);

	//Clearing out previous hashtable (setting to zero to clear it out)
	pl_buffer_set(gm->active_table->arena.base, 0, gm->active_table->arena.top);
	//setting new active table.
	gm->active_table = next_table;
}

void init_grid_processor(PL* pl, AppMemory* gm)
{

	gm->grid_processor_memory = MARENA_PUSH(&pl->memory.main_arena, sizeof(GPM), "Grid Processor Memory Struct");

	GPM *gpm = (GPM*)gm->grid_processor_memory;

	gpm->gpm_arena.capacity = Megabytes(25);
	init_memory_arena(&gpm->gpm_arena, gpm->gpm_arena.capacity, MARENA_PUSH(&pl->memory.main_arena, gpm->gpm_arena.capacity, "Grid Processor Memory Arena"));

	//NOTE: this is a temporary solution.
#ifdef MONITOR_ARENA_USAGE
	gpm->gpm_temp_arena.allocations.front = (ArenaOwnerNode*)pl_buffer_alloc(sizeof(ArenaOwnerNode) * ARENAOWNERLIST_CAPACITY);
#endif

	//hashtable stuff
	//table size needs to be a power of 2. 
	gm->table_size = { (2 << 20) };	//2^20 is essentially a hash table thats 4MB big (8MB on x64). 

	//NOTE: THESE HAVE TO BE THE SAME SIZE!
	gpm->table1.arena.capacity = Megabytes(10);
	gpm->table2.arena.capacity = gpm->table1.arena.capacity;


	init_memory_arena(&gpm->table1.arena, gpm->table1.arena.capacity, MARENA_PUSH(&gpm->gpm_arena, gpm->table1.arena.capacity, "Sub Arena: HashTable-1"));
	gpm->table1.table_front = (LiveCellNode**)MARENA_PUSH(&gpm->table1.arena, sizeof(LiveCellNode*) * gm->table_size, "HashTable-1 -> table");
	gpm->table1.node_list.init(&gpm->table1.arena, "HashTable-1 -> live node list");


	init_memory_arena(&gpm->table2.arena, gpm->table2.arena.capacity, MARENA_PUSH(&gpm->gpm_arena, gpm->table2.arena.capacity, "Sub Arena: HashTable-2"));
	gpm->table2.table_front = (LiveCellNode**)MARENA_PUSH(&gpm->table2.arena, sizeof(LiveCellNode*) * gm->table_size, "HashTable-2 -> table");
	gpm->table2.node_list.init(&gpm->table2.arena, "HashTable-2 -> live node list");

	gm->active_table = &gpm->table1;
	//---------------

}

void shutdown_grid_processor(PL* pl, AppMemory* gm)
{
	GPM* gpm = (GPM*)gm->grid_processor_memory;


	gpm->table2.node_list.clear(&gpm->table2.arena);
	MARENA_POP(&gpm->table2.arena, sizeof(LiveCellNode*) * gm->table_size, "HashTable-2 -> table");

	MARENA_POP(&gpm->gpm_arena, gpm->table2.arena.capacity, "Sub Arena: HashTable-2");

	gpm->table1.node_list.clear(&gpm->table1.arena);
	MARENA_POP(&gpm->table1.arena, sizeof(LiveCellNode*) * gm->table_size, "HashTable-1 -> table");

	MARENA_POP(&gpm->gpm_arena, gpm->table1.arena.capacity, "Sub Arena: HashTable-1");

#ifdef MONITOR_ARENA_USAGE
	pl_buffer_free(gpm->table1.arena.allocations.front);
#endif

#ifdef MONITOR_ARENA_USAGE
	pl_buffer_free(gpm->table2.arena.allocations.front);
#endif

#ifdef MONITOR_ARENA_USAGE
	pl_buffer_free(gpm->gpm_temp_arena.allocations.front);
#endif

#ifdef MONITOR_ARENA_USAGE
	pl_buffer_free(gpm->gpm_arena.allocations.front);
#endif

	MARENA_POP(&pl->memory.main_arena, gpm->gpm_arena.capacity, "Grid Processor Memory Arena");
	MARENA_POP(&pl->memory.main_arena, sizeof(GPM), "Grid Processor Memory Struct");
}

void cellgrid_update_step(PL* pl, AppMemory* gm)
{

	GPM* gpm = (GPM*)gm->grid_processor_memory;

	//initializing/allocating the temp arena with the remaining main arena memory every step. 
	gpm->gpm_temp_arena.capacity = gpm->gpm_arena.capacity - gpm->gpm_arena.top;
	gpm->gpm_temp_arena.overflow_addon_size = 0;
	gpm->gpm_temp_arena.top = 0;
	gpm->gpm_temp_arena.base = MARENA_PUSH(&gpm->gpm_arena, gpm->gpm_temp_arena.capacity, "Temp Arena");

	update_cellgrid(pl, gm);

	//resetting/clearing the temp arena at the end of the frame. 
	MARENA_POP(&gpm->gpm_arena, gpm->gpm_temp_arena.capacity, "Temp Arena");
}


static void process_cell(LiveCellNode* cell, AppMemory* gm, Hashtable* next_table, MSlice<WorldPos, uint32>& new_cells_tested)
{
	WorldPos* pos = &cell->pos;
	WorldPos lookup_pos[8];
	lookup_pos[0] = { pos->x    , pos->y - 1 };	//bm
	lookup_pos[1] = { pos->x    , pos->y + 1 };	//tm
	lookup_pos[2] = { pos->x + 1, pos->y - 1 };	//br
	lookup_pos[3] = { pos->x + 1, pos->y };		//mr
	lookup_pos[4] = { pos->x + 1, pos->y + 1 };	//tr
	lookup_pos[5] = { pos->x - 1, pos->y - 1 };	//bl
	lookup_pos[6] = { pos->x - 1, pos->y };		//ml
	lookup_pos[7] = { pos->x - 1, pos->y + 1 };	//tl

	uint32 lookup_pos_hash[8];
	//TODO: SIMD this.
	lookup_pos_hash[0] = hash_pos(lookup_pos[0], gm->table_size);
	lookup_pos_hash[1] = hash_pos(lookup_pos[1], gm->table_size);
	lookup_pos_hash[2] = hash_pos(lookup_pos[2], gm->table_size);
	lookup_pos_hash[3] = hash_pos(lookup_pos[3], gm->table_size);
	lookup_pos_hash[4] = hash_pos(lookup_pos[4], gm->table_size);
	lookup_pos_hash[5] = hash_pos(lookup_pos[5], gm->table_size);
	lookup_pos_hash[6] = hash_pos(lookup_pos[6], gm->table_size);
	lookup_pos_hash[7] = hash_pos(lookup_pos[7], gm->table_size);

	b32 surround_state[8] = {};

	uint32 active_around = 0;
	for (uint32 i = 0; i < ArrayCount(lookup_pos); i++)
	{
		uint32 slot = lookup_pos_hash[i];
		surround_state[i] = lookup_cell(gm->active_table, slot, lookup_pos[i]);
		active_around += surround_state[i];
	}

	if (active_around == 2 || active_around == 3)
	{
		//Cell survives! Adding to next hashmap. 
		uint32 slot = hash_pos(cell->pos, gm->table_size);
		append_new_node(next_table, slot, cell->pos);
	}
	//else cell doesn't survive to next state. 

	//Adding dead cells that are around the live cell to be processed at the end. 
	for (uint32 i = 0; i < ArrayCount(surround_state); i++)
	{
		if (surround_state[i] == FALSE)
		{
			//appending new cell to be processed. This is to ensure that the same surrounding 'off' cell isn't processed twice. 
			WorldPos new_cell_pos = lookup_pos[i];

			WorldPos* front = new_cells_tested.front;
			for (uint32 i = 0; i < new_cells_tested.size; i++)
			{
				if (front->x == new_cell_pos.x && front->y == new_cell_pos.y)
				{
					goto SKIP_TEST; //The cell already exists in the list to be processed. no need to add again. 
				}
				front++;
			}
			new_cells_tested.add(&gm->active_table->arena, new_cell_pos);


			//process new cell.
			WorldPos nc_lookup_pos[8];
			nc_lookup_pos[0] = { new_cell_pos.x    , new_cell_pos.y - 1 };	//bm
			nc_lookup_pos[1] = { new_cell_pos.x    , new_cell_pos.y + 1 };	//tm
			nc_lookup_pos[2] = { new_cell_pos.x + 1, new_cell_pos.y - 1 };	//br
			nc_lookup_pos[3] = { new_cell_pos.x + 1, new_cell_pos.y };		//mr
			nc_lookup_pos[4] = { new_cell_pos.x + 1, new_cell_pos.y + 1 };	//tr
			nc_lookup_pos[5] = { new_cell_pos.x - 1, new_cell_pos.y - 1 };	//bl
			nc_lookup_pos[6] = { new_cell_pos.x - 1, new_cell_pos.y };		//ml
			nc_lookup_pos[7] = { new_cell_pos.x - 1, new_cell_pos.y + 1 };	//tl

			b32 nc_surround_state[8] = { 2, 2, 2, 2, 2, 2, 2, 2 };	// 2 means not pre-assigned
			//preassigning the surrounding state with the already looked up ones. 
			for (uint32 j = 0; j < ArrayCount(nc_lookup_pos); j++)
			{
				for (uint32 ii = 0; ii < ArrayCount(lookup_pos); ii++)
				{
					if (nc_lookup_pos[j].x == lookup_pos[ii].x && nc_lookup_pos[j].y == lookup_pos[ii].y)
					{
						nc_surround_state[j] = surround_state[ii];
						break;
					}
				}
			}
			//performing lookups on the neighboring cells that aren't near the nearby live cell. 
			for (uint32 j = 0; j < ArrayCount(nc_lookup_pos); j++)
			{
				if (nc_surround_state[j] != 2)	//found by the previous lookup 
				{
					continue;
				}
				else   //performing lookup of cell.
				{
					uint32 nc_lookup_hash = hash_pos(nc_lookup_pos[j], gm->table_size);
					uint32 nc_slot = nc_lookup_hash;
					nc_surround_state[j] = lookup_cell(gm->active_table, nc_slot, nc_lookup_pos[j]);
				}
			}

			//now with the completed nc_surrounding_state table, we can judge whether the cell is turned alive or not. 
			uint32 nc_active_count = 0;
			for (uint32 j = 0; j < ArrayCount(nc_surround_state); j++)
			{
				nc_active_count += nc_surround_state[j];
				ASSERT(nc_surround_state[j] == 0 || nc_surround_state[j] == 1);
			}

			if (nc_active_count == 3)	//cell becomes alive!
			{
				//adding cell to next hashmap
				uint32 nc_new_cell_hash = hash_pos(new_cell_pos, gm->table_size);
				uint32 nc_new_cell_index = nc_new_cell_hash;
				append_new_node(next_table, nc_new_cell_index, new_cell_pos);
			}
		}
	SKIP_TEST:;

	}
}
