/* -*- c-basic-offset: 4; indent-tabs-mode: nil -*- */
/* ====================================================================
 * Copyright (c) 1999-2010 Carnegie Mellon University.  All rights
 * reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * This work was supported in part by funding from the Defense Advanced
 * Research Projects Agency and the National Science Foundation of the
 * United States of America, and the CMU Sphinx Speech Consortium.
 *
 * THIS SOFTWARE IS PROVIDED BY CARNEGIE MELLON UNIVERSITY ``AS IS'' AND
 * ANY EXPRESSED OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL CARNEGIE MELLON UNIVERSITY
 * NOR ITS EMPLOYEES BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * ====================================================================
 *
 */
/*
 * continuous.c - Simple pocketsphinx command-line application to test
 *                both continuous listening/silence filtering from microphone
 *                and continuous file transcription.
 */

/*
 * This is a simple example of pocketsphinx application that uses continuous
 * listening
 * with silence filtering to automatically segment a continuous stream of audio
 * input
 * into utterances that are then decoded.
 *
 * Remarks:
 *   - Each utterance is ended when a silence segment of at least 1 sec is
 * recognized.
 *   - Single-threaded implementation for portability.
 *   - Uses audio library; can be replaced with an equivalent custom library.
 */

#include <assert.h>
#include <stdio.h>

#include <cstring>
#include <iostream>
#include <stdlib.h>
#include <string>

#if defined(_WIN32) && !defined(__CYGWIN__)
#include <windows.h>
#else
#include <sys/select.h>
#endif

#include <sphinxbase/ad.h>
#include <sphinxbase/err.h>

#include "pocketsphinx.h"

static const arg_t cont_args_def[] = {
    POCKETSPHINX_OPTIONS,
    /* Argument file. */
    {"-argfile", ARG_STRING, NULL, "Argument file giving extra arguments."},
    {"-adcdev", ARG_STRING, NULL, "Name of audio device to use for input."},
    {"-infile", ARG_STRING, NULL, "Audio file to transcribe."},
    {"-inmic", ARG_BOOLEAN, "no", "Transcribe audio from microphone."},
    {"-time", ARG_BOOLEAN, "no", "Print word times in file transcription."},
    CMDLN_EMPTY_OPTION};

static ps_decoder_t *ps_;
static cmd_ln_t *config_;
static FILE *rawfd_;

void process_rules(const char *hyp) {
  std::string cmd_ever, cmd_arc, cmd_clear, cmd_stop, cmd_ipaddr, cmd_halt,
      cmd_timer, cmd_timer1, cmd_timer2, cmd_timer3, cmd_timer4, cmd_timer5,
      cmd_timer10, cmd_timer10s;

  cmd_ever = "./everloop_demo &";
  cmd_arc = "./arc_demo &";
  cmd_ipaddr = "./ipaddress_demo &";
  cmd_clear = "./clear_demo &";
  cmd_halt = "sudo halt";

  cmd_timer = "./timer_demo &";
  cmd_timer1 = "./timer_demo 60 &";
  cmd_timer2 = "./timer_demo 120 &";
  cmd_timer3 = "./timer_demo 180 &";
  cmd_timer4 = "./timer_demo 240 &";
  cmd_timer5 = "./timer_demo 300 &";
  cmd_timer10 = "./timer_demo 600 &";
  cmd_timer10s = "./timer_demo 10 &";

  cmd_stop = "killall everloop_demo arc_demo timer_demo &";

  printf("match: %s\n", hyp);
  if (std::strcmp(hyp, "MATRIX EVERLOOP") == 0)
    system(cmd_ever.c_str());
  if (std::strcmp(hyp, "MATRIX ARC") == 0)
    system(cmd_arc.c_str());
  if (std::strcmp(hyp, "MATRIX STOP") == 0)
    system(cmd_stop.c_str());
  if (std::strcmp(hyp, "MATRIX IPADDRESS") == 0)
    system(cmd_ipaddr.c_str());
  if (std::strcmp(hyp, "MATRIX GAME TIME") == 0)
    system(cmd_timer.c_str());
  if (std::strcmp(hyp, "MATRIX ONE MINUTE") == 0)
    system(cmd_timer1.c_str());
  if (std::strcmp(hyp, "MATRIX TWO MINUTES") == 0)
    system(cmd_timer2.c_str());
  if (std::strcmp(hyp, "MATRIX THREE MINUTES") == 0)
    system(cmd_timer3.c_str());
  if (std::strcmp(hyp, "MATRIX FOUR MINUTES") == 0)
    system(cmd_timer4.c_str());
  if (std::strcmp(hyp, "MATRIX FIVE MINUTES") == 0)
    system(cmd_timer5.c_str());
  if (std::strcmp(hyp, "MATRIX TEN MINUTES") == 0)
    system(cmd_timer10.c_str());
  if (std::strcmp(hyp, "MATRIX TEN SECONDS") == 0)
    system(cmd_timer10s.c_str());

  if (std::strcmp(hyp, "MATRIX CLEAR") == 0) {
    system(cmd_stop.c_str());
    system(cmd_clear.c_str());
    system(cmd_clear.c_str());
  }
  if (std::strcmp(hyp, "MATRIX SHUTDOWN NOW") == 0) {
    system(cmd_stop.c_str());
    system(cmd_clear.c_str());
    system(cmd_clear.c_str());
    system(cmd_halt.c_str());
  }
}

static void print_word_times() {
  int frame_rate = cmd_ln_int32_r(config_, "-frate");
  ps_seg_t *iter = ps_seg_iter(ps_);
  while (iter != NULL) {
    int32 sf, ef, pprob;
    float conf;

    ps_seg_frames(iter, &sf, &ef);
    pprob = ps_seg_prob(iter, NULL, NULL, NULL);
    conf = logmath_exp(ps_get_logmath(ps_), pprob);
    printf("%s %.3f %.3f %f\n", ps_seg_word(iter), ((float)sf / frame_rate),
           ((float)ef / frame_rate), conf);
    iter = ps_seg_next(iter);
  }
}

static int check_wav_header(char *header, int expected_sr) {
  int sr;

  if (header[34] != 0x10) {
    E_ERROR("Input audio file has [%d] bits per sample instead of 16\n",
            header[34]);
    return 0;
  }
  if (header[20] != 0x1) {
    E_ERROR("Input audio file has compression [%d] and not required PCM\n",
            header[20]);
    return 0;
  }
  if (header[22] != 0x1) {
    E_ERROR(
        "Input audio file has [%d] channels, expected single channel mono\n",
        header[22]);
    return 0;
  }
  sr = ((header[24] & 0xFF) | ((header[25] & 0xFF) << 8) |
        ((header[26] & 0xFF) << 16) | ((header[27] & 0xFF) << 24));
  if (sr != expected_sr) {
    E_ERROR("Input audio file has sample rate [%d], but decoder expects [%d]\n",
            sr, expected_sr);
    return 0;
  }
  return 1;
}

/*
 * Continuous recognition from a file
 */
static void recognize_from_file() {
  int16 adbuf[2048];
  const char *fname;
  const char *hyp;
  int32 k;
  uint8 utt_started, in_speech;
  int32 print_times = cmd_ln_boolean_r(config_, "-time");

  fname = cmd_ln_str_r(config_, "-infile");
  if ((rawfd_ = fopen(fname, "rb")) == NULL) {
    E_FATAL_SYSTEM("Failed to open file '%s' for reading", fname);
  }

  if (strlen(fname) > 4 && strcmp(fname + strlen(fname) - 4, ".wav") == 0) {
    char waveheader[44];
    fread(waveheader, 1, 44, rawfd_);
    if (!check_wav_header(waveheader,
                          (int)cmd_ln_float32_r(config_, "-samprate")))
      E_FATAL("Failed to process file '%s' due to format mismatch.\n", fname);
  }

  if (strlen(fname) > 4 && strcmp(fname + strlen(fname) - 4, ".mp3") == 0) {
    E_FATAL("Can not decode mp3 files, convert input file to WAV 16kHz 16-bit "
            "mono before decoding.\n");
  }

  ps_start_utt(ps_);
  utt_started = FALSE;

  while ((k = fread(adbuf, sizeof(int16), 2048, rawfd_)) > 0) {
    ps_process_raw(ps_, adbuf, k, FALSE, FALSE);
    in_speech = ps_get_in_speech(ps_);
    if (in_speech && !utt_started) {
      utt_started = TRUE;
    }
    if (!in_speech && utt_started) {
      ps_end_utt(ps_);
      hyp = ps_get_hyp(ps_, NULL);
      if (hyp != NULL)
        printf("%s\n", hyp);
      if (print_times)
        print_word_times();
      fflush(stdout);

      ps_start_utt(ps_);
      utt_started = FALSE;
    }
  }
  ps_end_utt(ps_);
  if (utt_started) {
    hyp = ps_get_hyp(ps_, NULL);
    if (hyp != NULL) {
      printf("%s\n", hyp);
      if (print_times) {
        print_word_times();
      }
    }
  }

  fclose(rawfd_);
}

/* Sleep for specified msec */
static void sleep_msec(int32 ms) {
#if (defined(_WIN32) && !defined(GNUWINCE)) || defined(_WIN32_WCE)
  Sleep(ms);
#else
  /* ------------------- Unix ------------------ */
  struct timeval tmo;

  tmo.tv_sec = 0;
  tmo.tv_usec = ms * 1000;

  select(0, NULL, NULL, NULL, &tmo);
#endif
}

/*
 * Main utterance processing loop:
 *     for (;;) {
 *        start utterance and wait for speech to process
 *        decoding till end-of-utterance silence will be detected
 *        print utterance result;
 *     }
 */
static void recognize_from_microphone() {
  ad_rec_t *ad;
  int16 adbuf[2048];
  uint8 utt_started, in_speech;
  int32 k;
  const char *hyp;

  if ((ad = ad_open_dev(cmd_ln_str_r(config_, "-adcdev"),
                        (int)cmd_ln_float32_r(config_, "-samprate"))) == NULL)
    E_FATAL("Failed to open audio device\n");
  if (ad_start_rec(ad) < 0)
    E_FATAL("Failed to start recording\n");

  if (ps_start_utt(ps_) < 0)
    E_FATAL("Failed to start utterance\n");
  utt_started = FALSE;
  E_INFO("Ready....\n");

  for (;;) {
    if ((k = ad_read(ad, adbuf, 2048)) < 0)
      E_FATAL("Failed to read audio\n");
    ps_process_raw(ps_, adbuf, k, FALSE, FALSE);
    in_speech = ps_get_in_speech(ps_);
    if (in_speech && !utt_started) {
      utt_started = TRUE;
      E_INFO("Listening...\n");
    }
    if (!in_speech && utt_started) {
      /* speech -> silence transition, time to start new utterance  */
      ps_end_utt(ps_);
      hyp = ps_get_hyp(ps_, NULL);
      if (hyp != NULL) {
        process_rules(hyp);
        fflush(stdout);
      }

      if (ps_start_utt(ps_) < 0)
        E_FATAL("Failed to start utterance\n");
      utt_started = FALSE;
      E_INFO("Ready....\n");
    }
    sleep_msec(10);
  }
  ad_close(ad);
}

int main(int argc, char *argv[]) {
  char const *cfg;

  config_ = cmd_ln_parse_r(NULL, cont_args_def, argc, argv, TRUE);

  /* Handle argument file as -argfile. */
  if (config_ && (cfg = cmd_ln_str_r(config_, "-argfile")) != NULL) {
    config_ = cmd_ln_parse_file_r(config_, cont_args_def, cfg, FALSE);
  }

  if (config_ == NULL || (cmd_ln_str_r(config_, "-infile") == NULL &&
                         cmd_ln_boolean_r(config_, "-inmic") == FALSE)) {
    E_INFO("Specify '-infile <file.wav>' to recognize from file or '-inmic "
           "yes' to recognize from microphone.\n");
    cmd_ln_free_r(config_);
    return 1;
  }

  ps_default_search_args(config_);
  ps_ = ps_init(config_);
  if (ps_ == NULL) {
    cmd_ln_free_r(config_);
    return 1;
  }

  E_INFO("%s COMPILED ON: %s, AT: %s\n\n", argv[0], __DATE__, __TIME__);

  if (cmd_ln_str_r(config_, "-infile") != NULL) {
    recognize_from_file();
  } else if (cmd_ln_boolean_r(config_, "-inmic")) {
    recognize_from_microphone();
  }

  ps_free(ps_);
  cmd_ln_free_r(config_);

  return 0;
}
