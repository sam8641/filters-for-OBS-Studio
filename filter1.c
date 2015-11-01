//  This deinterlace plugin is for use in OBS Studio
//	Copyright (c) 2015 Samuel Williams
//
//	This program is free software; you can redistribute it and/or modify
//	it under the terms of the GNU General Public License as published by
//	the Free Software Foundation; either version 2 of the License, or
//	(at your option) any later version.
//
//	This program is distributed in the hope that it will be useful,
//	but WITHOUT ANY WARRANTY; without even the implied warranty of
//	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//	GNU General Public License for more details.
//
//	You should have received a copy of the GNU General Public License
//	along with this program; if not, write to the Free Software
//	Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA




#include <obs-module.h>
#include <obs-source.h>
#include <obs.h>
#include <util/platform.h>
#include <obs-internal.h>

struct filter1_data {
	obs_source_t                   *context;

	gs_effect_t                    *effect;
	gs_eparam_t                    *texture_width, *texture_height;
	gs_eparam_t                    *field1;

	float                          texwidth, texheight;

	bool bff; // bottom field first
	bool doubleframe;
	uint64_t prevTimingAdjust;
};

static const char *filter1_getname(void *unused)
{
	UNUSED_PARAMETER(unused);
	return obs_module_text("DeinterlaceBob");
}

static void filter1_update(void *data, obs_data_t *settings)
{
	struct filter1_data *filter = data;
	filter->bff = obs_data_get_bool(settings, "bff");
	filter->doubleframe = obs_data_get_bool(settings, "doubleframe");
}

static void filter1_destroy(void *data)
{
	struct filter1_data *filter = data;

	if (filter->effect) {
		obs_enter_graphics();
		gs_effect_destroy(filter->effect);
		obs_leave_graphics();
	}

	bfree(data);
}

static void *filter1_create(obs_data_t *settings, obs_source_t *context)
{
	struct filter1_data *filter =
		bzalloc(sizeof(struct filter1_data));
	char *effect_path = obs_module_file("DeinterlaceBob.effect");

	filter->context = context;

	obs_enter_graphics();

	filter->effect = gs_effect_create_from_file(effect_path, NULL);
	if (filter->effect) {
		filter->texture_width = gs_effect_get_param_by_name(
			filter->effect, "texture_width");
		filter->texture_height = gs_effect_get_param_by_name(
			filter->effect, "texture_height");
		filter->field1 = gs_effect_get_param_by_name(
			filter->effect, "field1");
	}

	obs_leave_graphics();

	bfree(effect_path);

	if (!filter->effect) {
		filter1_destroy(filter);
		return NULL;
	}

	filter1_update(filter, settings);

	filter->bff = false;
	return filter;
}

static void filter1_render(void *data, gs_effect_t *effect)
{
	struct filter1_data *filter = data;
	if (!filter) return;
	if (!obs_filter_get_target(filter->context)) return;

	effect;

	obs_source_process_filter_begin(filter->context, GS_RGBA,
		OBS_NO_DIRECT_RENDERING);

	bool renderBottomField = filter->bff;

	if(filter->doubleframe)
	{
		// Got a better way to determine if its a new frame?
		obs_source_t *context1 = filter->context;
		while(context1->filter_target)
			context1 = context1->filter_target;
		if(context1->timing_adjust != filter->prevTimingAdjust)
			renderBottomField = !renderBottomField;
	}

	filter->texwidth =(float)obs_source_get_width(
			obs_filter_get_target(filter->context));
	filter->texheight = (float)obs_source_get_height(
			obs_filter_get_target(filter->context));

	gs_effect_set_float(filter->texture_width, filter->texwidth);
	gs_effect_set_float(filter->texture_height, filter->texheight);
	gs_effect_set_float(filter->field1, renderBottomField);

	obs_source_process_filter_end(filter->context, filter->effect, 0, 0);

	//blog(LOG_WARNING, "test %p",data);

	UNUSED_PARAMETER(effect);
}

static obs_properties_t *filter1_properties(void *data)
{
	obs_properties_t *props = obs_properties_create();

	obs_properties_add_bool(props, "bff","BottomFieldFirst", 0, 1, 1);
	obs_properties_add_bool(props, "doubleframe","DoubleFrame", 0, 1, 1);

	UNUSED_PARAMETER(data);
	return props;
}

static void filter1_defaults(obs_data_t *settings)
{
	obs_data_set_default_bool(settings, "bff", 1);
	obs_data_set_default_bool(settings, "doubleframe", 0);
}


static void filter1_tick(void *data, float seconds)
{
	struct filter1_data *filter = data;
	if(filter->doubleframe)
	{
		// Got a better way to determine if its a new frame?
		obs_source_t *context1 = filter->context;
		while(context1->filter_target)
			context1 = context1->filter_target;
		filter->prevTimingAdjust = context1->timing_adjust;
	}
}


struct obs_source_info filter1_filter = {
	.id = "filter1_filter",
	.type = OBS_SOURCE_TYPE_FILTER,
	.output_flags = OBS_SOURCE_VIDEO,
	.get_name = filter1_getname,
	.create = filter1_create,
	.destroy = filter1_destroy,
	.update = filter1_update,
	.video_render = filter1_render,
	.get_properties = filter1_properties,
	.get_defaults = filter1_defaults,
	.video_tick = filter1_tick
};
