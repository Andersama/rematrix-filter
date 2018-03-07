#pragma once
#include <obs-module.h>
#include <stdio.h>

#include <media-io/audio-math.h>
#include <math.h>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("rematrix-filter", "en-US")

#define MT_ obs_module_text

#ifndef MAX_AUDIO_SIZE
#ifndef AUDIO_OUTPUT_FRAMES
#define	AUDIO_OUTPUT_FRAMES 1024
#endif
#define	MAX_AUDIO_SIZE (AUDIO_OUTPUT_FRAMES * sizeof(float))
#endif // !MAX_AUDIO_SIZE

/*****************************************************************************/
long long get_obs_output_channels() {
	// get channel number from output speaker layout set by obs
	struct obs_audio_info aoi;
	obs_get_audio_info(&aoi);
	long long recorded_channels = get_audio_channels(aoi.speakers);
	return recorded_channels;
}

/*****************************************************************************/
struct rematrix_data {
	obs_source_t *context;
	size_t channels;
	//store the routing information
	long route[MAX_AV_PLANES];
	double gain[MAX_AV_PLANES];
	//store a temporary buffer
	uint8_t *tmpbuffer[MAX_AV_PLANES];
};

/*****************************************************************************/
static const char *rematrix_name(void *unused) {
	UNUSED_PARAMETER(unused);
	return MT_("Rematrix");
}

/*****************************************************************************/
static void rematrix_destroy(void *data) {
	struct rematrix_data *rematrix = data;

	for (size_t i = 0; i < rematrix->channels; i++) {
		if (rematrix->tmpbuffer[i])
			bfree(rematrix->tmpbuffer[i]);
	}

	bfree(rematrix);
}

/*****************************************************************************/
static void rematrix_update(void *data, obs_data_t *settings) {
	struct rematrix_data *rematrix = data;

	rematrix->channels = audio_output_get_channels(obs_get_audio());

	bool route_changed = false;
	bool gain_changed = false;
	long route[MAX_AV_PLANES];
	double gain[MAX_AV_PLANES];

	//make enough space for c strings
	int pad_digits = (int)floor(log10(abs(MAX_AV_PLANES))) + 1;

	//template out the route format
	const char* route_name_format = "route %i";
	size_t route_len = strlen(route_name_format) + pad_digits;
	char* route_name = (char *)calloc(route_len, sizeof(char));

	//template out the gain format
	const char* gain_name_format = "gain %i";
	size_t gain_len = strlen(gain_name_format) + pad_digits;
	char* gain_name = (char *)calloc(gain_len, sizeof(char));
	
	//copy the routing over from the settings
	for (long long i = 0; i < MAX_AV_PLANES; i++) {
		sprintf(route_name, route_name_format, i);
		sprintf(gain_name, gain_name_format, i);

		route[i] = (int)obs_data_get_int(settings, route_name);
		gain[i] = (float)obs_data_get_double(settings, gain_name);
		
		gain[i] = db_to_mul(gain[i]);

		if (rematrix->route[i] != route[i]) {
			rematrix->route[i] = route[i];
			route_changed = true;
		}
		if (rematrix->gain[i] != gain[i]) {
			rematrix->gain[i] = gain[i];
			gain_changed = true;
		}
	}

	//don't memory leak
	free(route_name);
	free(gain_name);
}

/*****************************************************************************/
static void *rematrix_create(obs_data_t *settings, obs_source_t *filter) {
	struct rematrix_data *rematrix = bzalloc(sizeof(*rematrix));
	rematrix->context = filter;
	rematrix_update(rematrix, settings);

	for (size_t i = 0; i < rematrix->channels; i++) {
		rematrix->tmpbuffer[i] = bzalloc(MAX_AUDIO_SIZE);
	}

	return rematrix;
}

/*****************************************************************************/
static struct obs_audio_data *rematrix_filter_audio(void *data,
	struct obs_audio_data *audio) {

	//initialize once, optimize for fast use
	static volatile long long route[MAX_AV_PLANES];
	static volatile double gain[MAX_AV_PLANES];

	struct rematrix_data *rematrix = data;
	const size_t channels = rematrix->channels;
	float **fmatrixed_data = (float**)rematrix->tmpbuffer;
	float **fdata = (float**)audio->data;
	size_t ch_buffer = (audio->frames * sizeof(float));

	//prevent race condition
	for (size_t c = 0; c < channels; c++) {
		route[c] = rematrix->route[c];
		gain[c] = rematrix->gain[c];
	}

	uint32_t frames = audio->frames;
	size_t copy_size = 0;
	size_t unprocessed_samples = 0;
	//consume AUDIO_OUTPUT_FRAMES or less # of frames
	for (size_t chunk = 0; chunk < frames; chunk += AUDIO_OUTPUT_FRAMES) {
		//calculate the size of the data we're about to try to copy
		if (frames - chunk < AUDIO_OUTPUT_FRAMES)
			unprocessed_samples = frames - chunk;
		else
			unprocessed_samples = AUDIO_OUTPUT_FRAMES;
		copy_size = unprocessed_samples * sizeof(float);

		//copy data to temporary buffer
		for (size_t c = 0; c < channels; c++) {
			//valid route copy data to temporary buffer
			if (fdata[c] && route[c] >= 0 && route[c] < channels)
				memcpy(fmatrixed_data[c],
					&fdata[route[c]][chunk],
					copy_size);
			//not a valid route, mute
			else
				memset(fmatrixed_data[c], 0, MAX_AUDIO_SIZE);
		}

		//move data into place and process gain
		for(size_t c = 0; c < channels; c++){
			if(!fdata[c])
				continue;
			for (size_t s = 0; s < unprocessed_samples; s++) {
				fdata[c][chunk + s] = fmatrixed_data[c][s] * gain[c];
			}
		}
		//move to next chunk of unprocessed data
	}
	return audio;
}

/*****************************************************************************/
static void rematrix_defaults(obs_data_t *settings)
{
	//make enough space for c strings
	int pad_digits = (int)floor(log10(abs(MAX_AV_PLANES))) + 1;

	//template out the route format
	const char* route_name_format = "route %i";
	size_t route_len = strlen(route_name_format) + pad_digits;
	char* route_name = (char *)calloc(route_len, sizeof(char));

	//template out the gain format
	const char* gain_name_format = "gain %i";
	size_t gain_len = strlen(gain_name_format) + pad_digits;
	char* gain_name = (char *)calloc(gain_len, sizeof(char));

	//default is no routing (ordered) -1 or any out of bounds is mute*
	for (long long i = 0; i < MAX_AV_PLANES; i++) {
		sprintf(route_name, route_name_format, i);
		sprintf(gain_name, gain_name_format, i);

		obs_data_set_default_int(settings, route_name, i);
		obs_data_set_default_double(settings, gain_name, 0.0);
	}

	obs_data_set_default_string(settings, "profile_name", MT_("Default"));

	//don't memory leak
	free(gain_name);
	free(route_name);
}

/*****************************************************************************/
static bool fill_out_channels(obs_properties_t *props, obs_property_t *list,
	obs_data_t *settings) {

	obs_property_list_clear(list);
	obs_property_list_add_int(list, MT_("mute"), -1);
	long long channels = get_obs_output_channels();

	//make enough space for c strings
	int pad_digits = (int)floor(log10(abs(MAX_AV_PLANES))) + 1;

	//template out the format for the json
	const char* route_obs_format = "in.ch.%i";
	size_t route_obs_len = strlen(route_obs_format) + pad_digits;
	char* route_obs = (char *)calloc(route_obs_len, sizeof(char));

	for (long long c = 0; c < channels; c++) {
		sprintf(route_obs, route_obs_format, c);
		obs_property_list_add_int(list, MT_(route_obs), c);
	}

	//don't memory leak
	free(route_obs);

	return true;
}

/*****************************************************************************/
static obs_properties_t *rematrix_properties(void *data)
{
	UNUSED_PARAMETER(data);

	obs_properties_t *props = obs_properties_create();

	//make a list long enough for the maximum # of chs
	obs_property_t *route[MAX_AV_PLANES];
	//pseduo-pan w/ gain (thanks Matt)
	obs_property_t *gain[MAX_AV_PLANES];

	size_t channels = audio_output_get_channels(obs_get_audio());

	//make enough space for c strings
	int pad_digits = (int)floor(log10(abs(MAX_AV_PLANES))) + 1;

	//template out the route format
	const char* route_name_format = "route %i";
	size_t route_len = strlen(route_name_format) + pad_digits;
	char* route_name = (char *)calloc(route_len, sizeof(char));

	//template out the format for the json
	const char* route_obs_format = "out.ch.%i";
	size_t route_obs_len = strlen(route_obs_format) + pad_digits;
	char* route_obs = (char *)calloc(route_obs_len, sizeof(char));

	//template out the gain format
	const char* gain_name_format = "gain %i";
	size_t gain_len = strlen(gain_name_format) + pad_digits;
	char* gain_name = (char *)calloc(gain_len, sizeof(char));
	
	//add an appropriate # of options to mix from
	for (size_t i = 0; i < channels; i++) {
		sprintf(route_name, route_name_format, i);
		sprintf(gain_name, gain_name_format, i);

		sprintf(route_obs, route_obs_format, i);
		route[i] = obs_properties_add_list(props, route_name,
			MT_(route_obs), OBS_COMBO_TYPE_LIST,
			OBS_COMBO_FORMAT_INT);

		obs_property_set_long_description(route[i],
			MT_("tooltip"));

		obs_property_set_modified_callback(route[i],
			fill_out_channels);

		gain[i] = obs_properties_add_float_slider(props, gain_name,
			MT_("Gain.GainDB"), -30.0, 30.0, 0.1);
	}

	//don't memory leak
	free(gain_name);
	free(route_name);
	free(route_obs);

	return props;
}

/*****************************************************************************/
bool obs_module_load(void)
{
	struct obs_source_info rematrixer_filter = {
		.id = "rematrix_filter",
		.type = OBS_SOURCE_TYPE_FILTER,
		.output_flags = OBS_SOURCE_AUDIO,
		.get_name = rematrix_name,
		.create = rematrix_create,
		.destroy = rematrix_destroy,
		.update = rematrix_update,
		.filter_audio = rematrix_filter_audio,
		.get_defaults = rematrix_defaults,
		.get_properties = rematrix_properties,
	};

	obs_register_source(&rematrixer_filter);
	return true;
}