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

#define SCALE 100.0f

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
	float mix[MAX_AUDIO_CHANNELS][MAX_AUDIO_CHANNELS];

	double gain[MAX_AUDIO_CHANNELS];
	//store a temporary buffer
	uint8_t *tmpbuffer[MAX_AUDIO_CHANNELS];
	//ensure we can treat it as a dynamic array
	size_t size;
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
	bool mix_changed = false;

	double gain[MAX_AUDIO_CHANNELS];
	float mix[MAX_AUDIO_CHANNELS][MAX_AUDIO_CHANNELS];

	//make enough space for c strings
	int pad_digits = (int)floor(log10(abs(MAX_AUDIO_CHANNELS))) + 1;

	//template out the route format
	const char* route_name_format = "route %i";
	size_t route_len = strlen(route_name_format) + pad_digits;
	char* route_name = (char *)calloc(route_len, sizeof(char));

	//template out the gain format
	const char* gain_name_format = "gain %i";
	size_t gain_len = strlen(gain_name_format) + pad_digits;
	char* gain_name = (char *)calloc(gain_len, sizeof(char));

	//template out mix format
	const char* mix_name_format = "mix.%i.%i";
	size_t mix_len = strlen(mix_name_format) + (pad_digits * 2);
	char* mix_name = (char *)calloc(mix_len, sizeof(char));

	//copy the routing over from the settings
	for (long long i = 0; i < MAX_AUDIO_CHANNELS; i++) {
		for (long long j = 0; j < MAX_AUDIO_CHANNELS; j++) {
			sprintf(mix_name, mix_name_format, i, j);
			mix[i][j] = (float)obs_data_get_double(settings, mix_name) / SCALE;
			
			if (rematrix->mix[i][j] != mix[i][j]) {
				rematrix->mix[i][j] = mix[i][j];
				mix_changed = true;
			}
		}
		sprintf(route_name, route_name_format, i);
		sprintf(gain_name, gain_name_format, i);

		gain[i] = (float)obs_data_get_double(settings, gain_name);

		gain[i] = db_to_mul(gain[i]);

		if (rematrix->gain[i] != gain[i]) {
			rematrix->gain[i] = gain[i];
			gain_changed = true;
		}
	}

	//don't memory leak
	free(route_name);
	free(gain_name);
	free(mix_name);
}

/*****************************************************************************/
static void *rematrix_create(obs_data_t *settings, obs_source_t *filter) {
	struct rematrix_data *rematrix = bzalloc(sizeof(*rematrix));
	rematrix->context = filter;
	rematrix_update(rematrix, settings);

	size_t target_size = MAX_AUDIO_SIZE;
	for (size_t i = 0; i < rematrix->channels; i++) {
		rematrix->tmpbuffer[i] = (uint8_t*)bzalloc(target_size);
		while (target_size >= 64 && !rematrix->tmpbuffer[i]) {
			rematrix->tmpbuffer[i] = (uint8_t*)bzalloc(target_size);
			blog(LOG_ERROR, "%s failed allocation for buffer %i, shrinking buffer size to %llu", __FUNCTION__, i, target_size);
			target_size /= 2; //cut size in half
		}
		if (!rematrix->tmpbuffer[i]) {
			blog(LOG_ERROR, "%s failed minimum allocation", __FUNCTION__, i);
		}
	}
	rematrix->size = target_size;

	return rematrix;
}

/*****************************************************************************/
static struct obs_audio_data *rematrix_filter_audio(void *data,
	struct obs_audio_data *audio) {

	//initialize once, optimize for fast use
	static volatile long long ch_count[MAX_AUDIO_CHANNELS];
	static volatile double gain[MAX_AUDIO_CHANNELS];
	static volatile float mix[MAX_AUDIO_CHANNELS][MAX_AUDIO_CHANNELS];
	static volatile double true_gain[MAX_AUDIO_CHANNELS];
	static volatile uint32_t options[MAX_AUDIO_CHANNELS];

	struct rematrix_data *rematrix = data;
	const size_t channels = rematrix->channels;
	float **fmatrixed_data = (float**)rematrix->tmpbuffer;
	float **fdata = (float**)audio->data;
	size_t ch_buffer = (audio->frames * sizeof(float));

	//prevent race condition
	for (size_t c = 0; c < channels; c++) {
		ch_count[c] = 0;
		gain[c] = rematrix->gain[c];
		for (size_t c2 = 0; c2 < channels; c2++) {
			mix[c][c2] = rematrix->mix[c][c2];
			//use ch_count to "count" how many chs are in use
			//for normalization
			if (mix[c][c2] > 0)
				ch_count[c]++;
		}
		//when ch_count == 0.0 float returns +inf, making this division by 0.0 actually safe
		if (ch_count[c] == 0.0) {
			true_gain[c] = 0.0;
		}
		else {
			true_gain[c] = gain[c] / ch_count[c];
		}
	}

	uint32_t frames = audio->frames;
	size_t copy_size = 0;
	size_t unprocessed_samples = 0;
	//consume AUDIO_OUTPUT_FRAMES or less # of frames
	/*
	__m256 s256;
	__m256 m256;
	__m256 t256;
	*/
	__m128 s128;
	__m128 m128;
	__m128 t128;
	size_t size = rematrix->size / sizeof(float);
	for (size_t chunk = 0; chunk < frames; chunk += size) {
		//calculate the size of the data we're about to try to copy
		if (frames - chunk < size)
			unprocessed_samples = frames - chunk;
		else
			unprocessed_samples = size;
		copy_size = unprocessed_samples * sizeof(float);

		//copy data to temporary buffer
		for (size_t c = 0; c < channels; c++) {
			//reset to 0
			size_t c2 = 0;
			for (; c2 < 1; c2++) {
				if (fdata[c] && mix[c][c2]) {
					size_t s = 0;
					size_t end;
					/*
					end = unprocessed_samples - 8;
					for(; s < end; s+=8) {
						s256 = _mm256_load_ps(&fdata[c2][chunk + s]);
						m256 = _mm256_set1_ps(mix[c][c2]);//_mm256_load_ps(&mix[c][c2]);
						_mm256_store_ps(&fmatrixed_data[c][s], _mm256_mul_ps(s256, m256));
					}
					*/
					end = unprocessed_samples - 4;
					for(; s < end; s+=4) {
						s128 = _mm_load_ps(&fdata[c2][chunk + s]);
						m128 = _mm_set1_ps(mix[c][c2]);//_mm256_load_ps(&mix[c][c2]);
						_mm_store_ps(&fmatrixed_data[c][s], _mm_mul_ps(s128, m128));
					}
					for (; s < unprocessed_samples; s++) {
						fmatrixed_data[c][s] = fdata[c2][chunk + s] * mix[c][c2];
					}
				} else {
					memset(fmatrixed_data[c], 0, copy_size);
				}
			}
			//memset(fmatrixed_data[c], 0, copy_size);
			//add contributions
			for (; c2 < channels; c2++) {
				if (fdata[c] && mix[c][c2]) {
					size_t s = 0;
					size_t end;
					/*
					end = unprocessed_samples - 8;
					for(; s < end; s+=8) {
						s256 = _mm256_load_ps(&fdata[c2][chunk + s]);
						t256 = _mm256_load_ps(&fmatrixed_data[c][s]);
						m256 = _mm256_set1_ps(mix[c][c2]);//_mm256_load_ps(&mix[c][c2]);
						_mm256_store_ps(&fmatrixed_data[c][s], _mm256_add_ps(t256, _mm256_mul_ps(s256, m256)));
					}
					*/
					end = unprocessed_samples - 4;
					for(; s < end; s+=4) {
						s128 = _mm_load_ps(&fdata[c2][chunk + s]);
						t128 = _mm_load_ps(&fmatrixed_data[c][s]);
						m128 = _mm_set1_ps(mix[c][c2]);//_mm256_load_ps(&mix[c][c2]);
						_mm_store_ps(&fmatrixed_data[c][s], _mm_add_ps(t128, _mm_mul_ps(s128, m128)));
					}
					for (; s < unprocessed_samples; s++) {
						fmatrixed_data[c][s] += fdata[c2][chunk + s] * mix[c][c2];
					}
				}
			}
		}
		//move data into place and process gain
		for (size_t c = 0; c < channels; c++) {
			if(!fdata[c])
				continue;
			size_t s = 0;
			size_t end;
			/*
			end = unprocessed_samples - 8;
			for(; s < end; s+=8){
				s256 = _mm256_load_ps(&(fmatrixed_data[c][s]));
				m256 = _mm256_set1_ps(true_gain[c]);
				_mm256_store_ps(&fdata[c][chunk + s], _mm256_mul_ps(s256, m256));
			}
			*/
			end = unprocessed_samples - 4;
			for(; s < end; s+=4){
				s128 = _mm_load_ps(&(fmatrixed_data[c][s]));
				m128 = _mm_set1_ps(true_gain[c]);
				_mm_store_ps(&fdata[c][chunk + s], _mm_mul_ps(s128, m128));
			}
			for (; s < unprocessed_samples; s++) {
				fdata[c][chunk + s] = fmatrixed_data[c][s] * true_gain[c];
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
	int pad_digits = (int)floor(log10(abs(MAX_AUDIO_CHANNELS))) + 1;

	//template out the gain format
	const char* gain_name_format = "gain %i";
	size_t gain_len = strlen(gain_name_format) + pad_digits;
	char* gain_name = (char *)calloc(gain_len, sizeof(char));

	//template out mix format
	const char* mix_name_format = "mix.%i.%i";
	size_t mix_len = strlen(gain_name_format) + (pad_digits * 2);
	char* mix_name = (char *)calloc(gain_len, sizeof(char));

	//default is no routing (ordered) -1 or any out of bounds is mute*
	for (long long i = 0; i < MAX_AUDIO_CHANNELS; i++) {
		for (long long j = 0; j < MAX_AUDIO_CHANNELS; j++) {
			sprintf(mix_name, mix_name_format, i, j);
			//default mix is a ch to itself
			if (i == j) {
				obs_data_set_default_double(settings, mix_name, 1.0 * SCALE);
			}
			else {
				obs_data_set_default_double(settings, mix_name, 0.0);
			}
		}

		sprintf(gain_name, gain_name_format, i);

		obs_data_set_default_double(settings, gain_name, 0.0);
	}

	obs_data_set_default_string(settings, "profile_name", MT_("Default"));

	//don't memory leak
	free(gain_name);
	free(mix_name);
}

static bool update_visible(obs_properties_t *props, obs_property_t *prop,
	obs_data_t *settings) {
	int selected_ch = obs_data_get_int(settings, obs_property_name(prop));
	long long channels = get_obs_output_channels();
	size_t i = 0;
	size_t j = 0;

	//make enough space for c strings
	int pad_digits = (int)floor(log10(abs(MAX_AUDIO_CHANNELS))) + 1;

	//template out
	const char* route_obs_format = "out.ch.%i";
	size_t route_obs_len = strlen(route_obs_format) + pad_digits;
	char* route_obs = (char *)calloc(route_obs_len, sizeof(char));

	if (obs_property_get_type(prop) == OBS_PROPERTY_LIST) {
		obs_property_list_clear(prop);

		for (i = 0; i < channels; i++) {
			sprintf(route_obs, route_obs_format, i);
			obs_property_list_add_int(prop, MT_(route_obs), i);
		}
	}

	//template out the gain format
	const char* gain_name_format = "gain %i";
	size_t gain_len = strlen(gain_name_format) + pad_digits;
	char* gain_name = (char *)calloc(gain_len, sizeof(char));

	//template out mix format
	const char* mix_name_format = "mix.%i.%i";
	size_t mix_len = strlen(mix_name_format) + (pad_digits * 2);
	char* mix_name = (char *)calloc(mix_len, sizeof(char));

	obs_property_t *gain;
	obs_property_t *mix;

	bool visible = false;

	for (i = 0; i < channels; i++) {
		sprintf(gain_name, gain_name_format, i);
		
		visible = i == selected_ch;

		gain = obs_properties_get(props, gain_name);
		obs_property_set_visible(gain,visible);

		for (j = 0; j < channels; j++) {
			sprintf(mix_name, mix_name_format, i, j);
			mix = obs_properties_get(props, mix_name);
			obs_property_set_visible(mix, visible);
		}
	}

	free(route_obs);
	free(gain_name);
	free(mix_name);

	return true;
}

/*****************************************************************************/
static obs_properties_t *rematrix_properties(void *data)
{
	UNUSED_PARAMETER(data);

	obs_properties_t *props = obs_properties_create();

	//make a list long enough for the maximum # of chs
	obs_property_t *route[MAX_AUDIO_CHANNELS];
	//pseduo-pan w/ gain (thanks Matt)
	obs_property_t *gain[MAX_AUDIO_CHANNELS];

	obs_property_t *view_route;

	size_t channels = audio_output_get_channels(obs_get_audio());

	//make enough space for c strings
	int pad_digits = (int)floor(log10(abs(MAX_AUDIO_CHANNELS))) + 1;

	//template out the route format
	const char* route_name_format = "route %i";
	size_t route_len = strlen(route_name_format) + pad_digits;
	char* route_name = (char *)calloc(route_len, sizeof(char));

	//template out the format for the json
	const char* route_obs_format = "in.ch.%i";
	size_t route_obs_len = strlen(route_obs_format) + pad_digits;
	char* route_obs = (char *)calloc(route_obs_len, sizeof(char));

	//template out the gain format
	const char* gain_name_format = "gain %i";
	size_t gain_len = strlen(gain_name_format) + pad_digits;
	char* gain_name = (char *)calloc(gain_len, sizeof(char));

	//template out mix format
	const char* mix_name_format = "mix.%i.%i";
	size_t mix_len = strlen(mix_name_format) + (pad_digits * 2);
	char* mix_name = (char *)calloc(mix_len, sizeof(char));

	//obs_property_add_int_slider(props, "view.route", MT("route"), 0, channels, 1);
	//view_route = obs_properties_add_int_slider(props, "view.route", MT_("route"), 0, channels - 1, 1);

	for (size_t i = 0; i < channels; i++) {
		sprintf(gain_name, gain_name_format, i);
		gain[i] = obs_properties_add_float_slider(props, gain_name,
			MT_("Gain.GainDB"), -30.0, 30.0, 0.1);
	}

	view_route = obs_properties_add_list(props, "view.route", MT_("route"),
		OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);

	obs_property_set_modified_callback(view_route, update_visible);

	//add an appropriate # of options to mix from
	for (size_t i = 0; i < channels; i++) {
		sprintf(route_name, route_name_format, i);
		for (long long j = 0; j < channels; j++) {
			sprintf(mix_name, mix_name_format, i, j);
			sprintf(route_obs, route_obs_format, j);
			obs_properties_add_float_slider(props, mix_name,
				MT_(route_obs), 0.0, 1.0 * SCALE, 0.01);
		}
	}

	//don't memory leak
	free(gain_name);
	free(route_name);
	free(route_obs);
	free(mix_name);

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