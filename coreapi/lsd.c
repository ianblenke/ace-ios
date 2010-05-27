/*
linphone
Copyright (C) 2010 Simon MORLAT (simon.morlat@linphone.org)

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
*/

/* Linphone Sound Daemon: is a lightweight utility to play sounds to speaker during a conversation.
 This is useful for embedded platforms, where sound apis are not performant enough to allow
 simultaneous sound access.
*/

#include "linphonecore_utils.h"
#include "mediastreamer2/msticker.h"
#include "mediastreamer2/mssndcard.h"
#include "mediastreamer2/msaudiomixer.h"
#include "mediastreamer2/mschanadapter.h"
#include "mediastreamer2/msfileplayer.h"
#include "mediastreamer2/msitc.h"



#define MAX_BRANCHES 10


struct _LsdPlayer{
	struct _LinphoneSoundDaemon *lsd;
	MSFilter *player;
	MSFilter *rateconv;
	MSFilter *chanadapter;
	LsdEndOfPlayCallback eop_cb;
	int mixer_pin;
	void *user_data;
};

struct _LinphoneSoundDaemon {
	int out_rate;
	int out_nchans;
	MSFilter *mixer;
	MSFilter *soundout;
	MSFilter *itcsink;
	MSTicker *ticker;
	MSSndCard *proxycard;
	LsdPlayer branches[MAX_BRANCHES];
};

static MSFilter *create_writer(MSSndCard *c){
	LinphoneSoundDaemon *lsd=(LinphoneSoundDaemon*)c->data;
	return lsd->itcsink;
}

static MSSndCardDesc proxycard={
	"Linphone Sound Daemon",
	/*detect*/ NULL,
	/*init*/ NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	/*create_reader*/ NULL,
	create_writer,
	/*uninit,*/
};

LsdPlayer *linphone_sound_daemon_get_player(LinphoneSoundDaemon *obj){
	int i;
	for(i=0;i<MAX_BRANCHES;++i){
		LsdPlayer *b=&obj->branches[i];
		MSFilter *p=b->player;
		int state;
		ms_filter_call_method(p,MS_PLAYER_GET_STATE,&state);
		if (state==MSPlayerClosed){
			lsd_player_set_gain(b,1);
			return b;
		}
	}
	ms_warning("No more free players !");
	return NULL;
}

void linphone_sound_daemon_release_player(LinphoneSoundDaemon *obj, LsdPlayer * player){
	int state;
	ms_filter_call_method(player->player,MS_PLAYER_GET_STATE,&state);
	if (state!=MSPlayerClosed){
		ms_filter_call_method(player->player,MS_PLAYER_CLOSE,&state);
	}
}

int lsd_player_stop(LsdPlayer *p){
	ms_filter_call_method_noarg(p->player,MS_PLAYER_PAUSE);
	return 0;
}

static void lsd_player_init(LsdPlayer *p, MSConnectionPoint mixer, MSFilterId playerid, LinphoneSoundDaemon *lsd){
	MSConnectionHelper h;
	p->player=ms_filter_new(playerid);
	p->rateconv=ms_filter_new(MS_RESAMPLE_ID);
	p->chanadapter=ms_filter_new(MS_CHANNEL_ADAPTER_ID);
	
	ms_connection_helper_start(&h);
	ms_connection_helper_link(&h,p->player,-1,0);
	ms_connection_helper_link(&h,p->rateconv,0,0);
	ms_connection_helper_link(&h,p->chanadapter,0,0);
	ms_connection_helper_link(&h,mixer.filter,mixer.pin,-1);
	p->mixer_pin=mixer.pin;
	p->lsd=lsd;
}

static void lsd_player_uninit(LsdPlayer *p, MSConnectionPoint mixer){
	MSConnectionHelper h;

	ms_connection_helper_start(&h);
	ms_connection_helper_unlink (&h,p->player,-1,0);
	ms_connection_helper_unlink(&h,p->rateconv,0,0);
	ms_connection_helper_unlink(&h,p->chanadapter,0,0);
	ms_connection_helper_unlink(&h,mixer.filter,mixer.pin,-1);

	ms_filter_destroy(p->player);
	ms_filter_destroy(p->rateconv);
	ms_filter_destroy(p->chanadapter);
}

void lsd_player_set_callback(LsdPlayer *p, LsdEndOfPlayCallback cb){
	p->eop_cb=cb;
}

void lsd_player_set_user_pointer(LsdPlayer *p, void *up){
	p->user_data=up;
}

void *lsd_player_get_user_pointer(LsdPlayer *p){
	return p->user_data;
}

static void lsd_player_on_eop(void * userdata, unsigned int id, void *arg){
}

int lsd_player_play(LsdPlayer *b, const char *filename ){
	int rate,chans;
	int state;
	LinphoneSoundDaemon *lsd=b->lsd;
	
	ms_filter_call_method(b->player,MS_PLAYER_GET_STATE,&state);
	if (state!=MSPlayerClosed){
		ms_filter_call_method_noarg(b->player,MS_PLAYER_CLOSE);
	}
	
	if (ms_filter_call_method(b->player,MS_PLAYER_OPEN,(void*)filename)!=0){
		return -1;
	}
	ms_filter_call_method(b->player,MS_FILTER_GET_SAMPLE_RATE,&rate);
	ms_filter_call_method(b->player,MS_FILTER_GET_NCHANNELS,&chans);
	ms_filter_set_notify_callback (b->player,lsd_player_on_eop,b);
	
	ms_filter_call_method(b->rateconv,MS_FILTER_SET_SAMPLE_RATE,&rate);
	ms_filter_call_method(b->rateconv,MS_FILTER_SET_NCHANNELS,&chans);
	ms_filter_call_method(b->rateconv,MS_FILTER_SET_OUTPUT_SAMPLE_RATE,&lsd->out_rate);

	ms_filter_call_method(b->chanadapter,MS_FILTER_SET_NCHANNELS,&chans);
	ms_filter_call_method(b->chanadapter,MS_CHANNEL_ADAPTER_SET_OUTPUT_NCHANNELS,&lsd->out_nchans);
	return 0;
}

int lsd_player_stop(LsdPlayer *p);

void lsd_player_enable_loop(LsdPlayer *p, bool_t loopmode){
	if (ms_filter_get_id(p->player)==MS_FILE_PLAYER_ID){
		int arg=loopmode ? 0 : -1;
		ms_filter_call_method(p->player,MS_FILE_PLAYER_LOOP,&arg);
	}
}

void lsd_player_set_gain(LsdPlayer *p, float gain){
	MSAudioMixerCtl gainctl;
	gainctl.pin=p->mixer_pin;
	gainctl.gain=gain;
	ms_filter_call_method(p->lsd->mixer,MS_AUDIO_MIXER_SET_INPUT_GAIN,&gainctl);
}

LinphoneSoundDaemon * linphone_sound_daemon_new(const char *cardname){
	int i;
	MSConnectionPoint mp;
	LinphoneSoundDaemon *lsd;
	MSSndCard *card=ms_snd_card_manager_get_card(
	                                             ms_snd_card_manager_get(),
	                                             cardname);
	if (card==NULL){
		card=ms_snd_card_manager_get_default_playback_card (
		                                                    ms_snd_card_manager_get());
		if (card==NULL){
			ms_error("linphone_sound_daemon_new(): No playback soundcard available");
			return NULL;
		}
	}
	
	lsd=ms_new0(LinphoneSoundDaemon,1);
	lsd->soundout=ms_snd_card_create_writer(card);
	lsd->mixer=ms_filter_new(MS_AUDIO_MIXER_ID);
	lsd->itcsink=ms_filter_new(MS_ITC_SINK_ID);
	lsd->out_rate=44100;
	lsd->out_nchans=2;
	ms_filter_call_method(lsd->soundout,MS_FILTER_SET_SAMPLE_RATE,&lsd->out_rate);
	ms_filter_call_method(lsd->soundout,MS_FILTER_SET_NCHANNELS,&lsd->out_nchans);
	ms_filter_call_method(lsd->mixer,MS_FILTER_SET_SAMPLE_RATE,&lsd->out_rate);
	ms_filter_call_method(lsd->mixer,MS_FILTER_SET_NCHANNELS,&lsd->out_nchans);

	mp.filter=lsd->mixer;
	mp.pin=0;

	lsd_player_init(&lsd->branches[0],mp,MS_ITC_SINK_ID,lsd);
	for(i=1;i<MAX_BRANCHES;++i){
		mp.pin=i;
		lsd_player_init(&lsd->branches[i],mp,MS_FILE_PLAYER_ID,lsd);
	}
	ms_filter_link(lsd->mixer,0,lsd->soundout,0);
	lsd->ticker=ms_ticker_new();
	ms_ticker_attach(lsd->ticker,lsd->soundout);

	lsd->proxycard=ms_snd_card_new(&proxycard);
	lsd->proxycard->data=lsd;

	ms_filter_call_method(lsd->itcsink,MS_ITC_SINK_CONNECT,lsd->branches[0].player);
	
	return lsd;
}

void linphone_sound_daemon_destroy(LinphoneSoundDaemon *obj){
	int i;
	MSConnectionPoint mp;
	ms_ticker_detach(obj->ticker,obj->soundout);
	mp.filter=obj->mixer;
	for(i=0;i<MAX_BRANCHES;++i){
		mp.pin=i;
		lsd_player_uninit (&obj->branches[i],mp);
	}
	ms_ticker_destroy(obj->ticker);
	ms_filter_destroy(obj->soundout);
	ms_filter_destroy(obj->mixer);
	ms_filter_destroy(obj->itcsink);
}



MSSndCard *linphone_sound_daemon_get_proxy_card(LinphoneSoundDaemon *lsd){
	return lsd->proxycard;
}
