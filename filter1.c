/* This deinterlace plugin is for use in OBS Studio

This filter depends on having the following functions in obs-source.h

struct obs_source_info:   int number_of_video_frames_needed;      // note: add at the bottom of all other variable/functions

EXPORT bool obs_source_get_source_interlace_fields(obs_source_t *source);
EXPORT gs_texture_t *obs_source_get_texture(obs_source_t *source, unsigned int i);


*/







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
//#include <obs-internal.h>


struct filter1_data {
	obs_source_t                   *context;

	gs_effect_t                    *effect;
	gs_eparam_t                    *texture_width, *texture_height;
	gs_eparam_t                    *field1;
	gs_eparam_t                    *framenumber1param;
	gs_eparam_t                    *image1param, *image2param;

	gs_texture_t                   *tex1, *tex2;
	gs_texrender_t                 *texr1, *texr2;
	gs_texrender_t                 *texr1a, *texr2a;

	float                          texwidth, texheight;

	int method;
	int methodLoaded;

	bool bff; // bottom field first
	bool doubleframe;
	bool framefield;
	float timepassed;
	uint64_t prevTimingAdjust;
};


// filenames are case sensetive on some platforms
static const char *effectmethods[] = {
	"deinterlacebob.effect",
	"deinterlaceblend.effect",
};

static const char *filter1_getname(void *unused)
{
	UNUSED_PARAMETER(unused);
	return obs_module_text("DeinterlaceBob");
}

static obs_properties_t *filter1_properties(void *data)
{
	obs_properties_t *props = obs_properties_create();

	obs_property_t *list = obs_properties_add_list(props, "method","method", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_insert_int(list, 0, "bob", 0);
	obs_property_list_insert_int(list, 1, "blend", 1);
	obs_properties_add_bool(props, "bff","BottomFieldFirst");
	obs_properties_add_bool(props, "doubleframe","DoubleFrame");

	UNUSED_PARAMETER(data);
	return props;
}

static void filter1_update(void *data, obs_data_t *settings)
{
	struct filter1_data *filter = data;
	filter->method = obs_data_get_int(settings, "method");
	filter->bff = obs_data_get_bool(settings, "bff");
	filter->doubleframe = obs_data_get_bool(settings, "doubleframe");
}

static void filter1_defaults(obs_data_t *settings)
{
	obs_data_set_default_int(settings, "method", 0);
	obs_data_set_default_bool(settings, "bff", 1);
	obs_data_set_default_bool(settings, "doubleframe", 0);
}


static void changeEffectShader(struct filter1_data *filter)
{
	filter->methodLoaded = filter->method;
	char *effect_path = obs_module_file(effectmethods[filter->method]);
	obs_enter_graphics();
	if (filter->effect)
		gs_effect_destroy(filter->effect);

	filter->effect = gs_effect_create_from_file(effect_path, NULL);
	if (filter->effect) {
		filter->texture_width = gs_effect_get_param_by_name(filter->effect, "texture_width");
		filter->texture_height = gs_effect_get_param_by_name(filter->effect, "texture_height");
		filter->field1 = gs_effect_get_param_by_name(filter->effect, "field1");
		filter->framenumber1param = gs_effect_get_param_by_name(filter->effect, "framenumber");
		filter->image1param = gs_effect_get_param_by_name(filter->effect, "image1");
		filter->image2param = gs_effect_get_param_by_name(filter->effect, "image2");

	}
	obs_leave_graphics();
	bfree(effect_path);
}

static void filter1_destroy_textures(struct filter1_data *filter)
{
	if (filter->texr1a) gs_texrender_destroy(filter->texr1a);
	if (filter->texr2a) gs_texrender_destroy(filter->texr2a);
	if (filter->texr1) gs_texrender_destroy(filter->texr1);
	if (filter->texr2) gs_texrender_destroy(filter->texr2);
	if (filter->tex1) gs_texture_destroy(filter->tex1);
	if (filter->tex2) gs_texture_destroy(filter->tex2);
}

static void filter1_destroy(void *data)
{
	struct filter1_data *filter = data;

	if (filter->effect) {
		obs_enter_graphics();
		gs_effect_destroy(filter->effect);
		filter1_destroy_textures(filter);
		obs_leave_graphics();
	}

	bfree(data);
}

static void *filter1_create(obs_data_t *settings, obs_source_t *context)
{
	struct filter1_data *filter =
		bzalloc(sizeof(struct filter1_data));

	filter->context = context;

	changeEffectShader(filter);


	if (!filter->effect) {
		filter1_destroy(filter);
		return NULL;
	}

	filter1_update(filter, settings);

	filter->bff = false;
	return filter;
}

static void filter1_setTextureBuffers1(struct filter1_data *filter)
{
	gs_texture_t *tex1 = obs_source_get_texture(filter->context, 0);
	gs_texture_t *tex2 = obs_source_get_texture(filter->context, 1);
	

	if(filter->image1param && tex1)
		gs_effect_set_texture(filter->image1param, tex1);
	if(filter->image2param && tex2)
		gs_effect_set_texture(filter->image2param, tex2);
}

static void filter1_render(void *data, gs_effect_t *effect)
{
	struct filter1_data *filter = data;
	if (!filter) return;
	if (!obs_filter_get_target(filter->context)) return;
	
	obs_source_process_filter_begin(filter->context, GS_RGBA,
		OBS_NO_DIRECT_RENDERING);

	bool renderBottomField = !filter->bff;


	if(filter->doubleframe)
	{
		renderBottomField = (renderBottomField != obs_source_get_source_interlace_fields(filter->context));
	}

	//if(filter->image2param != 0)
		filter1_setTextureBuffers1(filter);

	filter->texwidth =(float)obs_source_get_width(
			obs_filter_get_target(filter->context));
	filter->texheight = (float)obs_source_get_height(
			obs_filter_get_target(filter->context));
	
	gs_effect_set_float(filter->texture_width, filter->texwidth);
	gs_effect_set_float(filter->texture_height, filter->texheight);
	if(filter->field1)
		gs_effect_set_float(filter->field1, renderBottomField);
	if(filter->framenumber1param)
		gs_effect_set_float(filter->framenumber1param, (renderBottomField != filter->bff && filter->doubleframe) ? 1 : 0);

	obs_source_process_filter_end(filter->context, filter->effect, 0, 0);

	//blog(LOG_WARNING, "test %p",data);

	UNUSED_PARAMETER(effect);
}


static void filter1_tick(void *data, float seconds)
{
	struct filter1_data *filter = data;

	filter->timepassed = seconds;

	if(filter->methodLoaded != filter->method)
		changeEffectShader(filter);


}

struct obs_source_info filter1_filter = {
	.id = "filter1_filter",
	.type = OBS_SOURCE_TYPE_FILTER,
	.output_flags = OBS_SOURCE_VIDEO  /*| OBS_SOURCE_CUSTOM_DRAW | OBS_SOURCE_COLOR_MATRIX */,
	.get_name = filter1_getname,
	.create = filter1_create,
	.destroy = filter1_destroy,
	.update = filter1_update,
	.video_render = filter1_render,
	.get_properties = filter1_properties,
	.get_defaults = filter1_defaults,
	.video_tick = filter1_tick,
	.number_of_video_frames_needed = 2,
};
