#include "fzero_msu.h"
#include "fzero_gameplay.h"
#include "fzero_music_sources.h"
#include "fzero_tracks.h"
#include "fzero_packs.h"
#include "fzero_menu_music.h"
#include "fzero_course_runtime.h"
#include "fzero_local.h"
#include "fzero_online.h"
#include "common_rtl.h"
#include "cpu_state.h"
#include "snes/msu1.h"
#include "snes/snes.h"
#include "snes/apu.h"
#include "snes/dsp.h"

#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static bool legacy_save_root;
static bool host_music, host_pcm, host_loop;
static bool host_restored;
static bool host_seek_pending;
static uint64_t host_seek;
static unsigned host_command=~0u, host_course=~0u;
static uint64_t host_master;
static uint64_t host_generation;
extern Snes *g_snes;
static char error[256];
static FzeroMusicSources music_sources;
static char selected_source[96], resolved_directory[1024];
static bool selected_supported = true, selected_race, directory_loaded, directory_valid;
typedef struct MsuRoute {
  char source[96],pack[96];unsigned index;
  bool supported,race,course_music,soundtrack_music;
} MsuRoute;
static MsuRoute loaded_route;
static const MsuRoute *restoring_route;
static bool host_voices(uint8_t voices);

void FzeroMsuSelectTrackSource(const char *source, bool race) {
  selected_supported = source != NULL;
  selected_race = race;
  snprintf(selected_source, sizeof(selected_source), "%s", source ? source : "");
}
static bool resolve_track(void *context, const char *base, uint16_t track, char *path, size_t cap) {
  (void)context;
  /* Course filenames identify installed race music without soundtrack IDs,
   * prefixes or a numeric song map. Non-course events use the pack defaults
   * and individual selections exposed in Mods. */
  if(restoring_route) {
    loaded_route=*restoring_route;
    if(loaded_route.soundtrack_music)
      return FzeroPacksSoundtrackMusic(loaded_route.source,loaded_route.pack,loaded_route.index,path,cap);
    if(loaded_route.course_music)
      return FzeroPacksCourseMusic(loaded_route.pack,loaded_route.index,path,cap);
  } else {
    memset(&loaded_route,0,sizeof(loaded_route));
    snprintf(loaded_route.source,sizeof(loaded_route.source),"%s",selected_source);
    loaded_route.supported=selected_supported;loaded_route.race=selected_race;
    if(selected_race) {
      if(*FzeroPacksSelectedSoundtrack() && !FzeroPacksSoundtrackName(FzeroPacksSelectedSoundtrack())) return false;
      char target[96]; unsigned index;
      bool custom=FzeroTracksCurrentMusicKey(target,sizeof(target),&index);
      if(!custom && track>=10 && track<35) {
        snprintf(target,sizeof(target),"%s",FzeroBsTracks()?"bs-deluxe":"retail");
        index=track-10;
      }
      if((custom || (track>=10 && track<35)) &&
          FzeroPacksSoundtrackMusic(FzeroPacksSelectedSoundtrack(),target,index,path,cap)) {
        loaded_route.soundtrack_music=true;loaded_route.index=index;
        snprintf(loaded_route.pack,sizeof(loaded_route.pack),"%s",target);
        snprintf(loaded_route.source,sizeof(loaded_route.source),"%s",FzeroPacksSelectedSoundtrack());
        return true;
      }
    }
    if(selected_race && FzeroTracksCurrentMusic(path,cap)) {
      loaded_route.course_music=FzeroTracksCurrentMusicKey(loaded_route.pack,sizeof(loaded_route.pack),&loaded_route.index);
      return true;
    }
  }
  if (!selected_race) return FzeroMenuMusicResolve(track, path, cap);
  const char *leaf = base;
  for (const char *p = base; *p; ++p) if (*p == '/' || *p == '\\') leaf = p + 1;
  size_t length = (size_t)(leaf - base);
  if (length >= sizeof(resolved_directory)) return false;
  if (!directory_loaded || strlen(resolved_directory) != length || strncmp(resolved_directory, base, length)) {
    memcpy(resolved_directory, base, length); resolved_directory[length] = 0;
    directory_loaded = true;
    memset(&music_sources, 0, sizeof(music_sources));
    directory_valid = FzeroTracksReadMusicSources(&music_sources);
    if (!directory_valid)
      fprintf(stderr, "[fzero-msu1] Invalid soundtrack metadata; using SNES audio.\n");
  }
  if (!directory_valid || !selected_supported) return false;
  if (FzeroMusicResolve(&music_sources, base, selected_source, selected_race, track, path, cap)) {
    FILE *file = fopen(path, "rb");
    if (file) { fclose(file); return true; }
  }
  return FzeroPacksResolveMusic(selected_source, track, path, cap);
}
bool FzeroMsuActive(void) { return legacy_save_root; }
const char *FzeroMsuError(void) { return error; }

bool FzeroMsuHasInstalledMusic(void) { return FzeroPacksHasMusic(); }

static void set_pack(const char *value) {
#ifdef _WIN32
  _putenv_s("SNESRECOMP_MSU1", value);
#else
  setenv("SNESRECOMP_MSU1", value, 1);
#endif
}

static void trim_msu_suffix(char *base) {
  size_t n = strlen(base);
  if (n >= 2 && base[0] == '"' && base[n-1] == '"') {
    memmove(base, base+1, n-2); base[n-2]=0; n-=2;
  }
  if (n >= 4 && base[n-4] == '.' &&
      tolower((unsigned char)base[n-3]) == 'm' &&
      tolower((unsigned char)base[n-2]) == 's' &&
      tolower((unsigned char)base[n-1]) == 'u') base[n-4]=0;
}
bool FzeroMsuHasLegacyPatch(const char *pack) {
  if (!pack || !*pack) return false;
  const char *override = getenv("FZERO_MSU1_PATCH");
  if (override && *override) return true;
  char directory[1024], path[1200];
  if (strlen(pack) >= sizeof(directory)) return false;
  snprintf(directory,sizeof(directory),"%s",pack);
  trim_msu_suffix(directory);
  /* Preserve the legacy command-line shortcut for music beside the ROM. */
  if (!strcmp(directory,"auto") || !strcmp(directory,"on") || !strcmp(directory,"1"))
    return true;
  struct stat st;
  if (stat(directory,&st) != 0 || !(st.st_mode & S_IFDIR)) {
    char *slash=strrchr(directory,'/'), *back=strrchr(directory,'\\');
    if (!slash || (back && back>slash)) slash=back;
    if (slash) *slash=0; else snprintf(directory,sizeof(directory),".");
  }
  snprintf(path,sizeof(path),"%s/f-zero_msu1.ips",directory);
  return stat(path,&st)==0;
}
static bool configure(const char *pack, bool cgp, const char *rom_path, bool legacy_saves) {
  (void)host_voices(0xff);
  host_music=host_pcm=host_loop=host_restored=host_seek_pending=false;
  host_command=host_course=~0u;host_master=0;
  memset(&loaded_route,0,sizeof(loaded_route));restoring_route=NULL;
  legacy_save_root=false; error[0]=0;
  FzeroMsuSelectTrackSource("", false);
  directory_loaded = false;
  msu1_set_track_resolver(cgp ? resolve_track : NULL, NULL);
  char music_base[1024];
  if (snprintf(music_base, sizeof(music_base), "%s", pack ? pack : "") >= (int)sizeof(music_base))
    goto long_path;
  trim_msu_suffix(music_base);
  if (!strcmp(music_base, "off") || !strcmp(music_base, "0")) music_base[0] = 0;
  if (!strcmp(music_base, "auto") || !strcmp(music_base, "on") || !strcmp(music_base, "1")) {
    if (snprintf(music_base, sizeof(music_base), "%s", rom_path ? rom_path : "fzero.sfc") >=
        (int)sizeof(music_base)) goto long_path;
    char *dot = strrchr(music_base, '.'), *slash = strrchr(music_base, '/');
    char *back = strrchr(music_base, '\\');
    if (!slash || (back && back > slash)) slash = back;
    if (dot && (!slash || dot > slash)) *dot = 0;
  }
  host_music = *music_base != 0;
  legacy_save_root = host_music && !cgp && legacy_saves;
  set_pack(music_base);
  return true;
long_path:
  snprintf(error, sizeof(error), "Music path is too long");
  set_pack("");
  return false;
}

bool FzeroMsuConfigure(const char *pack, bool cgp, const char *rom_path) {
  return configure(pack, cgp, rom_path, true);
}

bool FzeroMsuConfigureVersus(const char *pack, bool cgp, const char *rom_path) {
  return configure(pack, cgp, rom_path, false);
}
static bool host_voices(uint8_t voices) {
  if(!g_snes || !g_snes->apu)return false;
  RtlApuLock();
  bool ok=dsp_set_output_voices(g_snes->apu->dsp,voices);
  RtlApuUnlock();return ok;
}
static unsigned music_command(void) {
  const FzeroOnlineState *online = FzeroOnlineCompleted();
  if (FzeroOnlineEnabled() && online->phase >= FZERO_ONLINE_MACHINES &&
      online->phase <= FZERO_ONLINE_COURSE)
    return 5;
  return FzeroLocalEnabled() ? FzeroLocalMusicCommand() : g_ram[0x46];
}

static unsigned music_location(void) {
  return FzeroLocalEnabled() ? FzeroLocalConfig().course : FzeroTracksMenuIndex() * 256 + g_ram[0x53];
}

void FzeroMsuUpdate(void) {
  if(!host_music || !g_snes)return;
  /* The load callback precedes the engine's generation increment. Acknowledge
   * that one timeline change without reselecting the just-restored track. */
  if(host_restored) {host_generation=RtlStateGeneration();host_restored=false;}
  const char *tag = FzeroLocalEnabled() ? "versus-msu" : "fzero-msu1";
  unsigned command=music_command();
  unsigned course=music_location();
  unsigned song=command&7;
  bool changed=host_command==~0u || song!=(host_command&7) ||
      (song==6 && course!=host_course) ||
      (!FzeroOnlineEnabled() && g_cpu.master_cycles<host_master) ||
      host_generation!=RtlStateGeneration();
  host_master=g_cpu.master_cycles;
  host_generation=RtlStateGeneration();
  if(changed) {
    host_command=song;host_course=course;host_pcm=false;
    msu1_write(0x2007,0);(void)host_voices(0xff);
    if(song && msu1_enabled()) {
      unsigned track=FzeroGameplayMusicTrack(song);
      msu1_write(0x2004,(uint8_t)track);msu1_write(0x2005,(uint8_t)(track>>8));
      if(!(msu1_read(0x2000)&8) && host_voices(0xc0)) {
        host_pcm=true;host_loop=song>3;
        msu1_write(0x2006,255);msu1_write(0x2007,host_loop?3:1);
      }
      fprintf(stderr,"[%s] command=%u course=%u track=%u output=%s\n",tag,song,course,track,host_pcm?"PCM":"SPC");
    }
    if(host_seek_pending) {
      if(host_pcm && !msu1_seek_playback(host_seek)) {
        host_pcm=false;(void)host_voices(0xff);
      }
      host_seek_pending=false;
    }
  }
  if(!host_pcm)return;
  /* A broken/empty looping file must restore native audio, not a silent race.
   * One-shot event songs are allowed to end normally. */
  if(host_loop && !(msu1_read(0x2000)&0x10)) {
    host_pcm=false;msu1_write(0x2007,0);(void)host_voices(0xff);
    fprintf(stderr,"[%s] Playback stopped unexpectedly; using SPC\n",tag);return;
  }
  unsigned volume=255;
  if((command&0x80) && (!FzeroLocalEnabled() || FzeroLocalSession())) {
    /* Follow the native SPC's result fade without changing its master or
     * stopping native engine, collision, drift and explosion channels. */
    RtlApuLock();int master=g_snes->apu->dsp->masterVolumeL;RtlApuUnlock();
    volume=master<=0?0:master>=0x54?255:(unsigned)master*255/0x54;
  }
  msu1_write(0x2006,(uint8_t)volume);
}
bool FzeroMsuSelectSaveRoot(void) {
  if (!legacy_save_root) return true;
  char root[96];
  if (snprintf(root, sizeof(root), "%s/msu1", RtlSaveRoot()) >= (int)sizeof(root)) {
    snprintf(error, sizeof(error), "MSU-1 save directory is too long; choose a shorter save root.");
    return false;
  }
  RtlEnsureSaveDir(); RtlSetSaveRoot(root); RtlEnsureSaveDir();
  return true;
}

static bool neutral_audio(const uint8_t *state) {
  static const uint8_t empty[FZERO_MSU_AUDIO_STATE_SIZE];
  return !memcmp(state,empty,sizeof(empty));
}
void FzeroMsuSaveAudio(uint8_t out[FZERO_MSU_AUDIO_STATE_SIZE]) {
  memset(out,0,FZERO_MSU_AUDIO_STATE_SIZE);
  if(RtlIsRollbackSave())return;
  msu1_capture_playback(out);
  snprintf((char *)out+32,96,"%s",selected_source);
  snprintf((char *)out+128,96,"%s",loaded_route.source);
  snprintf((char *)out+224,96,"%s",loaded_route.pack);
  out[320]=(uint8_t)loaded_route.index;out[321]=(uint8_t)(loaded_route.index>>8);
  out[322]=(uint8_t)(selected_supported | selected_race<<1 | loaded_route.supported<<2 |
      loaded_route.race<<3 | loaded_route.course_music<<4 | loaded_route.soundtrack_music<<5);
}
bool FzeroMsuAudioStateValid(const uint8_t state[FZERO_MSU_AUDIO_STATE_SIZE]) {
  if(neutral_audio(state))return true;
  if(!msu1_playback_state_valid(state) || (state[322]&~63) ||
      !memchr(state+32,0,96) || !memchr(state+128,0,96) || !memchr(state+224,0,96))return false;
  for(unsigned i=323;i<FZERO_MSU_AUDIO_STATE_SIZE;++i)if(state[i])return false;
  return (!(state[322]&48) || ((state[322]&8) && state[224])) &&
      (!(state[322]&32) || state[128]) && ((state[322]&48)!=48);
}
void FzeroMsuRestoreAudio(const uint8_t state[FZERO_MSU_AUDIO_STATE_SIZE]) {
  if(RtlIsRollbackLoad())return; /* speculative replay never rewinds live audio */
  if(neutral_audio(state))return;
  MsuRoute route={0};
  snprintf(route.source,sizeof(route.source),"%s",(const char *)state+128);
  snprintf(route.pack,sizeof(route.pack),"%s",(const char *)state+224);
  route.index=state[320]|(unsigned)state[321]<<8;
  route.supported=(state[322]&4)!=0;route.race=(state[322]&8)!=0;route.course_music=(state[322]&16)!=0;
  route.soundtrack_music=(state[322]&32)!=0;
  /* Reopen the actual saved course/namespace, including snapshots between
   * music selection and the guest's ready check. Restore the pending selector
   * afterwards. Only catalog identities travel in states, never audio paths. */
  FzeroMsuSelectTrackSource(route.supported?route.source:NULL,route.race);
  restoring_route=&route;
  bool restored=msu1_restore_playback(state);
  restoring_route=NULL;loaded_route=route;
  FzeroMsuSelectTrackSource((state[322]&1)?(const char *)state+32:NULL,(state[322]&2)!=0);
  bool pcm=restored && !(msu1_read(0x2000)&8) && (state[5]&1);
  if(host_music) {
    host_command=music_command()&7;host_course=music_location();
    host_pcm=host_command && pcm;host_loop=(state[5]&4)!=0;
    host_master=g_cpu.master_cycles;host_restored=true;
    host_seek_pending=false;
    if(FzeroLocalEnabled() && host_command && !(state[5]&1)) {
      /* A native-music host has no PCM cursor to share. Retain this peer's
       * MSU preference and align its own recording to the saved race clock. */
      const FzeroVersusSnapshot *race=FzeroVersusCompleted(FzeroLocalSession());
      uint64_t age=0;
      if(race)age=race->phase==FZERO_VS_COUNTDOWN?
          FZERO_VERSUS_COUNTDOWN-race->countdown:race->race_tick;
      double frames=(double)age*44100/FZERO_SIMULATION_HZ;
      host_seek=frames>=(double)UINT64_MAX?UINT64_MAX:(uint64_t)frames;
      host_command=~0u;host_seek_pending=true;
    }
    (void)host_voices(host_pcm?0xc0:0xff);
  } else {
    (void)host_voices(0xff);
  }
}
