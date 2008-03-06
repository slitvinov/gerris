/* Gerris - The GNU Flow Solver
 * Copyright (C) 2001 National Institute of Water and Atmospheric Research
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.  
 */

#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <errno.h>
#include <math.h>
#include "output.h"
#include "graphic.h"
#include "adaptive.h"
#include "solid.h"
#include "ocean.h"
#include "unstructured.h"

/* GfsOutput: object */

typedef struct _Format Format;

typedef enum {
  ITER,
  TIME,
  PID,
  NONE
} FormatType;

struct _Format {
  gchar * s;
  FormatType t;
};

static Format * format_new (gchar * s, guint len, 
			    FormatType t)
{
  Format * f = g_malloc (sizeof (Format));
  
  f->s = g_strndup (s, len);
  f->t = t;

  return f;
}

static void format_destroy (Format * f)
{
  g_free (f->s);
  g_free (f);
}

static gchar * format_string (GSList * list, 
			      gint pid, 
			      guint niter,
			      gdouble time)
{
  gchar * s = g_strdup ("");

  while (list) {
    Format * f = list->data;
    gchar * s1, * s2 = NULL;

    switch (f->t) {
    case NONE:
      s2 = g_strconcat (s, f->s, NULL);
      break;
    case PID:
      s1 = g_strdup_printf (f->s, pid);
      s2 = g_strconcat (s, s1, NULL);
      g_free (s1);
      break;
    case ITER:
      s1 = g_strdup_printf (f->s, niter);
      s2 = g_strconcat (s, s1, NULL);
      g_free (s1);
      break;
    case TIME:
      s1 = g_strdup_printf (f->s, time);
      s2 = g_strconcat (s, s1, NULL);
      g_free (s1);
      break;
    default:
      g_assert_not_reached ();
    }
    g_free (s);
    s = s2;
    list = list->next;
  }

  return s;
}

static void output_free (GfsOutput * output)
{
  if (output->format)
    g_free (output->format);
  output->format = NULL;
  g_slist_foreach (output->formats, (GFunc) format_destroy, NULL);
  g_slist_free (output->formats);
  output->formats = NULL;
}

static void gfs_output_destroy (GtsObject * object)
{
  GfsOutput * output = GFS_OUTPUT (object);

  if (output->file)
    gfs_output_file_close (output->file);
  output_free (output);

  (* GTS_OBJECT_CLASS (gfs_output_class ())->parent_class->destroy) 
    (object);
}

static gboolean gfs_output_event (GfsEvent * event, GfsSimulation * sim)
{
  if ((* gfs_event_class ()->event) (event, sim)) {
    GfsOutput * output = GFS_OUTPUT (event);
    gchar * fname;

    if (!output->dynamic) {
      if (output->file) {
	fflush (output->file->fp);
	output->first_call = FALSE;
      }
      else {
	if (output->format[0] == '{') { /* script */
	  guint len = strlen (output->format);
	  g_assert (output->format[len - 1] == '}');
	  output->format[len - 1] = '\0';
	  FILE * fp = gfs_popen (sim, &output->format[1], "w");
	  if (fp == NULL) {
	    g_warning ("GfsOutput cannot start script");
	    return TRUE;
	  }
	  output->file = gfs_output_file_new (fp);
	  output->file->is_pipe = TRUE;
	  output->format[len - 1] = '}';
	}
	else { /* standard file */
	  fname = format_string (output->formats,
				 GFS_DOMAIN (sim)->pid,
				 sim->time.i,
				 sim->time.t);
	  output->file = gfs_output_file_open (fname, 
					       sim->time.i > 0 && gfs_event_is_repetitive (event) ? 
					       "a" : "w");
	  if (output->file == NULL)
	    g_warning ("could not open file `%s'", fname);
	  g_free (fname);
	}
      }
      return (output->file != NULL);
    }

    if (output->file)
      gfs_output_file_close (output->file);
    fname = format_string (output->formats, 
			   GFS_DOMAIN (sim)->pid,
			   sim->time.i,
			   sim->time.t);
    output->file = gfs_output_file_open (fname, "w");
    if (output->file == NULL)
      g_warning ("could not open file `%s'", fname);
    g_free (fname);
    return (output->file != NULL);
  }
  return FALSE;
}

static void gfs_output_write (GtsObject * o, FILE * fp)
{
  GfsOutput * output = GFS_OUTPUT (o);

  (* GTS_OBJECT_CLASS (gfs_output_class ())->parent_class->write) (o, fp);

  if (output->format)
    fprintf (fp, " %s", output->format);
}

static void gfs_output_read (GtsObject ** o, GtsFile * fp)
{
  GfsOutput * output;

  (* GTS_OBJECT_CLASS (gfs_output_class ())->parent_class->read) (o, fp);
  if (fp->type == GTS_ERROR)
    return;

  output = GFS_OUTPUT (*o);
  if (output->file)
    gfs_output_file_close (output->file);
  output->file = NULL;
  if (output->format)
    g_free (output->format);
  output->format = NULL;
  output->dynamic = FALSE;
  output->first_call = TRUE;

  if (fp->type == '{') {
    gchar * script = gfs_file_statement (fp);
    if (script == NULL)
      return;
    output->format = g_strconcat ("{", script, "}", NULL);
    g_free (script);
    gts_file_next_token (fp);
  }
  else if (fp->type != GTS_STRING) {
    gts_file_error (fp, "expecting a string (format)");
    return;
  }
  else {
    gchar * c, * start, * fname, * fnamebak;
    FILE * fptr;
    guint len;

    output->format = g_strdup (fp->token->str);
    gts_file_next_token (fp);
    
    if (!strcmp (output->format, "stderr")) {
      output->file = gfs_output_file_open ("stderr", "w");
      return;
    }
    
    if (!strcmp (output->format, "stdout")) {
      output->file = gfs_output_file_open ("stdout", "w");
      return;
    }
    
    start = c = output->format;
    while (*c != '\0') {
      if (*c == '%') {
	gchar * startf = c, * prev = c;
	
	len = GPOINTER_TO_UINT (startf) -  GPOINTER_TO_UINT (start);
	if (len > 0)
	  output->formats = g_slist_prepend (output->formats,
					     format_new (start, len, NONE));
	
	len = 1;
	c++;
	while (*c != '\0' && !gfs_char_in_string (*c, "diouxXeEfFgGaAcsCSpn%")) {
	  prev = c;
	  c++;
	  len++;
	}
	len++;
	if (*c == '%')
	  output->formats = g_slist_prepend (output->formats,
					     format_new ("%", 1, NONE));
	else if (gfs_char_in_string (*c, "diouxXc")) {
	  if (*prev == 'l') {
	    output->formats = g_slist_prepend (output->formats,
					       format_new (startf, len, ITER));
	    output->dynamic = TRUE;
	  }
	  else
	    output->formats = g_slist_prepend (output->formats,
					       format_new (startf, len, PID));
	}
	else if (gfs_char_in_string (*c, "eEfFgGaA")) {
	  output->formats = g_slist_prepend (output->formats,
					     format_new (startf, len, TIME));
	  output->dynamic = TRUE;
	}
	else {
	  gts_file_error (fp, 
			  "unknown conversion specifier `%c' of format `%s'",
			  *c, output->format);
	  output_free (output);
	  return;
	}
	start = c;
	start++;
      }
      c++;
    }
    len = GPOINTER_TO_UINT (c) -  GPOINTER_TO_UINT (start);
    if (len > 0)
      output->formats = g_slist_prepend (output->formats,
					 format_new (start, len, NONE));
    output->formats = g_slist_reverse (output->formats);
    
    fname = format_string (output->formats, -1, 0, 0.);
    fnamebak = g_strconcat (fname, "~", NULL);
    g_free (fname);
    fptr = fopen (fnamebak, "w");
    if (fptr == NULL) {
      gts_file_error (fp, "cannot open file specified by format `%s'",
		      output->format);
      g_free (fnamebak);
      output_free (output);
      return;
    }
    fclose (fptr);
    remove (fnamebak);
    g_free (fnamebak);
  }
}

static void gfs_output_class_init (GfsOutputClass * klass)
{
  GFS_EVENT_CLASS (klass)->event = gfs_output_event;

  GTS_OBJECT_CLASS (klass)->write = gfs_output_write;
  GTS_OBJECT_CLASS (klass)->read = gfs_output_read;
  GTS_OBJECT_CLASS (klass)->destroy = gfs_output_destroy;
}

static void gfs_output_init (GfsOutput * object)
{
  object->file = NULL;
  object->format = NULL;
  object->formats = NULL;
  object->dynamic = FALSE;
  object->first_call = TRUE;
}

GfsOutputClass * gfs_output_class (void)
{
  static GfsOutputClass * klass = NULL;

  if (klass == NULL) {
    GtsObjectClassInfo gfs_output_info = {
      "GfsOutput",
      sizeof (GfsOutput),
      sizeof (GfsOutputClass),
      (GtsObjectClassInitFunc) gfs_output_class_init,
      (GtsObjectInitFunc) gfs_output_init,
      (GtsArgSetFunc) NULL,
      (GtsArgGetFunc) NULL
    };
    klass = gts_object_class_new (GTS_OBJECT_CLASS (gfs_event_class ()),
				  &gfs_output_info);
  }

  return klass;
}

/**
 * gfs_output_mute:
 * @output: a #GfsOutput.
 *
 * "Mutes" the output defined by @output, the event associated with
 * @output still takes place but the output itself is redirected to
 * /dev/null.
 */
void gfs_output_mute (GfsOutput * output)
{
  g_return_if_fail (output != NULL);

  output->dynamic = FALSE;
  if (output->file)
    gfs_output_file_close (output->file);
  output->file = gfs_output_file_open ("/dev/null", "w");
}

static GHashTable * gfs_output_files = NULL;

/**
 * gfs_output_file_new:
 * @fp: a file pointer.
 *
 * Returns: a new #GfsOutputFile for @fp.
 */
GfsOutputFile * gfs_output_file_new (FILE * fp)
{
  GfsOutputFile * file = g_malloc (sizeof (GfsOutputFile));
  file->refcount = 1;
  file->name = NULL;
  file->fp = fp;
  file->is_pipe = FALSE;
  return file;
}

/**
 * gfs_output_file_open:
 * @name: the name of the file to open.
 * @mode: the fopen mode.
 *
 * Checks whether @name has already been opened. If it has, its
 * reference count is incremented and the corresponding #GfsOutputFile
 * is returned. If it has not, it is created and opened for writing.
 *
 * Returns: the #GfsOutputFile of file @name.  
 */
GfsOutputFile * gfs_output_file_open (const gchar * name, const gchar * mode)
{
  GfsOutputFile * file;
  FILE * fp;

  g_return_val_if_fail (name != NULL, NULL);

  if (gfs_output_files == NULL) {
    gfs_output_files = g_hash_table_new (g_str_hash, g_str_equal);
    file = g_malloc (sizeof (GfsOutputFile));
    file->refcount = 2;
    file->name = g_strdup ("stderr");
    file->fp = stderr;
    g_hash_table_insert (gfs_output_files, file->name, file);
    file = g_malloc (sizeof (GfsOutputFile));
    file->refcount = 2;
    file->name = g_strdup ("stdout");
    file->fp = stdout;
    g_hash_table_insert (gfs_output_files, file->name, file);
  }

  if ((file = g_hash_table_lookup (gfs_output_files, name))) {
    file->refcount++;
    return file;
  }

  fp = fopen (name, mode);
  if (fp == NULL)
    return NULL;

  file = gfs_output_file_new (fp);
  file->name = g_strdup (name);
  g_hash_table_insert (gfs_output_files, file->name, file);

  return file;  
}

/**
 * gfs_output_file_close:
 * @file: a #GfsOutputFile.
 * 
 * Decreases the reference count of @file. If it reaches zero the file
 * corresponding to @file is closed and @file is freed.
 */
void gfs_output_file_close (GfsOutputFile * file)
{
  g_return_if_fail (file);

  file->refcount--;
  if (file->refcount == 0) {
    if (file->name)
      g_hash_table_remove (gfs_output_files, file->name);
    if (file->is_pipe)
      pclose (file->fp);
    else
      fclose (file->fp);
    g_free (file->name);
    g_free (file);
  }
}

/* GfsOutputTime: Object */

static void time_destroy (GtsObject * o)
{
  gfs_clock_destroy (GFS_OUTPUT_TIME (o)->clock);

  (* GTS_OBJECT_CLASS (gfs_output_time_class ())->parent_class->destroy) (o);  
}

static gboolean time_event (GfsEvent * event, GfsSimulation * sim)
{
  if ((* GFS_EVENT_CLASS (gfs_output_class())->event) (event, sim)) {
    if (!GFS_OUTPUT_TIME (event)->clock->started)
      gfs_clock_start (GFS_OUTPUT_TIME (event)->clock);
    fprintf (GFS_OUTPUT (event)->file->fp,
	     "step: %7u t: %15.8f dt: %13.6e cpu: %15.8f\n",
	     sim->time.i, sim->time.t, 
	     sim->advection_params.dt,
	     gfs_clock_elapsed (GFS_OUTPUT_TIME (event)->clock));
    return TRUE;
  }
  return FALSE;
}

static void gfs_output_time_class_init (GfsEventClass * klass)
{
  GTS_OBJECT_CLASS (klass)->destroy = time_destroy;
  klass->event = time_event;
}

static void gfs_output_time_init (GfsOutputTime * time)
{
  time->clock = gfs_clock_new ();
}

GfsOutputClass * gfs_output_time_class (void)
{
  static GfsOutputClass * klass = NULL;

  if (klass == NULL) {
    GtsObjectClassInfo gfs_output_time_info = {
      "GfsOutputTime",
      sizeof (GfsOutputTime),
      sizeof (GfsOutputClass),
      (GtsObjectClassInitFunc) gfs_output_time_class_init,
      (GtsObjectInitFunc) gfs_output_time_init,
      (GtsArgSetFunc) NULL,
      (GtsArgGetFunc) NULL
    };
    klass = gts_object_class_new (GTS_OBJECT_CLASS (gfs_output_class ()),
				  &gfs_output_time_info);
  }

  return klass;
}

/* GfsOutputProgress: Object */

static gboolean progress_event (GfsEvent * event, GfsSimulation * sim)
{
  if ((* GFS_EVENT_CLASS (gfs_output_class())->event) (event, sim)) {
    gdouble idone = sim->time.i/(gdouble) sim->time.iend;
    gdouble tdone = sim->time.t/sim->time.end;

    if (idone > tdone) tdone = idone;
    fprintf (GFS_OUTPUT (event)->file->fp,
	     "\r%3.0f%% done",
	     100.*tdone);
    if (tdone > 0.) {
      gdouble remaining = GFS_DOMAIN (sim)->timestep.sum*(1. - tdone)/tdone;
      gdouble hours = floor (remaining/3600.);
      gdouble mins = floor ((remaining - 3600.*hours)/60.);
      gdouble secs = floor (remaining - 3600.*hours - 60.*mins);
      fprintf (GFS_OUTPUT (event)->file->fp,
	       ", %02.0f:%02.0f:%02.0f remaining ",
	       hours, mins, secs);
    }
    if (tdone == 1.)
      fputc ('\n', GFS_OUTPUT (event)->file->fp);
    return TRUE;
  }
  return FALSE;
}

static void gfs_output_progress_class_init (GfsEventClass * klass)
{
  klass->event = progress_event;
}

GfsOutputClass * gfs_output_progress_class (void)
{
  static GfsOutputClass * klass = NULL;

  if (klass == NULL) {
    GtsObjectClassInfo gfs_output_progress_info = {
      "GfsOutputProgress",
      sizeof (GfsOutput),
      sizeof (GfsOutputClass),
      (GtsObjectClassInitFunc) gfs_output_progress_class_init,
      (GtsObjectInitFunc) NULL,
      (GtsArgSetFunc) NULL,
      (GtsArgGetFunc) NULL
    };
    klass = gts_object_class_new (GTS_OBJECT_CLASS (gfs_output_class ()),
				  &gfs_output_progress_info);
  }

  return klass;
}

/* GfsOutputProjectionStats: Object */

static gboolean projection_stats_event (GfsEvent * event, GfsSimulation * sim)
{
  if ((* GFS_EVENT_CLASS (gfs_output_class())->event) (event, sim)) {
    FILE * fp = GFS_OUTPUT (event)->file->fp;

    if (sim->projection_params.niter > 0) {
      fprintf (fp, "MAC projection        before     after       rate\n");
      gfs_multilevel_params_stats_write (&sim->projection_params, fp);
    }
    fprintf (fp, "Approximate projection\n");
    gfs_multilevel_params_stats_write (&sim->approx_projection_params, fp);
    return TRUE;
  }
  return FALSE;
}

static void gfs_output_projection_stats_class_init (GfsEventClass * klass)
{
  klass->event = projection_stats_event;
}

GfsOutputClass * gfs_output_projection_stats_class (void)
{
  static GfsOutputClass * klass = NULL;

  if (klass == NULL) {
    GtsObjectClassInfo gfs_output_projection_stats_info = {
      "GfsOutputProjectionStats",
      sizeof (GfsOutput),
      sizeof (GfsOutputClass),
      (GtsObjectClassInitFunc) gfs_output_projection_stats_class_init,
      (GtsObjectInitFunc) NULL,
      (GtsArgSetFunc) NULL,
      (GtsArgGetFunc) NULL
    };
    klass = gts_object_class_new (GTS_OBJECT_CLASS (gfs_output_class ()),
				  &gfs_output_projection_stats_info);
  }

  return klass;
}

/* GfsOutputDiffusionStats: Object */

static gboolean diffusion_stats_event (GfsEvent * event, GfsSimulation * sim)
{
  if ((* GFS_EVENT_CLASS (gfs_output_class())->event) (event, sim)) {
    FILE * fp = GFS_OUTPUT (event)->file->fp;
    GSList * l = NULL, * i;
    
    i = GFS_DOMAIN (sim)->variables;
    while (i) {
      GfsVariable * v = i->data;

      if (v->sources) {
	GSList * j = GTS_SLIST_CONTAINER (v->sources)->items;
    
	while (j) {
	  GtsObject * o = j->data;
      
	  if (GFS_IS_SOURCE_DIFFUSION (o) && !g_slist_find (l, o)) {
	    l = g_slist_prepend (l, o);
	    fprintf (fp, "%s diffusion\n", v->name);
	    gfs_multilevel_params_stats_write (&GFS_SOURCE_DIFFUSION (o)->D->par, fp);
	  }
	  j = j->next;
	}
      }
      i = i->next;
    }
    g_slist_free (l);
    return TRUE;
  }
  return FALSE;
}

static void gfs_output_diffusion_stats_class_init (GfsEventClass * klass)
{
  klass->event = diffusion_stats_event;
}

GfsOutputClass * gfs_output_diffusion_stats_class (void)
{
  static GfsOutputClass * klass = NULL;

  if (klass == NULL) {
    GtsObjectClassInfo gfs_output_diffusion_stats_info = {
      "GfsOutputDiffusionStats",
      sizeof (GfsOutput),
      sizeof (GfsOutputClass),
      (GtsObjectClassInitFunc) gfs_output_diffusion_stats_class_init,
      (GtsObjectInitFunc) NULL,
      (GtsArgSetFunc) NULL,
      (GtsArgGetFunc) NULL
    };
    klass = gts_object_class_new (GTS_OBJECT_CLASS (gfs_output_class ()),
				  &gfs_output_diffusion_stats_info);
  }

  return klass;
}

/* GfsOutputSolidStats: Object */

static gboolean gfs_output_solid_stats_event (GfsEvent * event, 
					     GfsSimulation * sim)
{
  if ((* GFS_EVENT_CLASS (GTS_OBJECT_CLASS (gfs_output_solid_stats_class ())->parent_class)->event)
      (event, sim)) {
    GtsRange stats = gfs_domain_stats_solid (GFS_DOMAIN (sim));
    GtsRange ma, mn;

    gfs_domain_stats_merged (GFS_DOMAIN (sim), &ma, &mn);
    fprintf (GFS_OUTPUT (event)->file->fp,
	     "Solid volume fraction\n"
	     "    min: %10.3e avg: %10.3e | %10.3e max: %10.3e n: %10d\n"
	     "Total merged solid volume fraction\n"
	     "    min: %10.3e avg: %10.3e | %10.3e max: %10.3e n: %10d\n"
	     "Number of cells merged per merged cell\n"
	     "    min: %10.0f avg: %10.3f | %10.3f max: %10.0f n: %10d\n"
	     "Number of \"thin\" cells removed: %10d\n",
	     stats.min, stats.mean, stats.stddev, stats.max, stats.n,
	     ma.min, ma.mean, ma.stddev, ma.max, ma.n,
	     mn.min, mn.mean, mn.stddev, mn.max, mn.n,
	     sim->thin);
    return TRUE;
  }
  return FALSE;
}

static void gfs_output_solid_stats_class_init (GfsOutputClass * klass)
{
  GFS_EVENT_CLASS (klass)->event = gfs_output_solid_stats_event;
}

GfsOutputClass * gfs_output_solid_stats_class (void)
{
  static GfsOutputClass * klass = NULL;

  if (klass == NULL) {
    GtsObjectClassInfo gfs_output_solid_stats_info = {
      "GfsOutputSolidStats",
      sizeof (GfsOutput),
      sizeof (GfsOutputClass),
      (GtsObjectClassInitFunc) gfs_output_solid_stats_class_init,
      (GtsObjectInitFunc) NULL,
      (GtsArgSetFunc) NULL,
      (GtsArgGetFunc) NULL
    };
    klass = gts_object_class_new (GTS_OBJECT_CLASS (gfs_output_class ()),
				  &gfs_output_solid_stats_info);
  }

  return klass;
}

/* GfsOutputAdaptStats: Object */

static gboolean gfs_output_adapt_stats_event (GfsEvent * event, 
					      GfsSimulation * sim)
{
  if ((* GFS_EVENT_CLASS (GTS_OBJECT_CLASS (gfs_output_adapt_stats_class ())->parent_class)->event)
      (event, sim)) {
    gfs_adapt_stats_update (&sim->adapts_stats);
    fprintf (GFS_OUTPUT (event)->file->fp,
	     "Adaptive mesh refinement statistics\n"
	     "  Cells removed: %10d\n"
	     "  Cells created: %10d\n"
	     "  Number of cells\n"
	     "    min: %10.0f avg: %10.3f | %10.3f max: %10.0f n: %10d\n",
	     sim->adapts_stats.removed,
	     sim->adapts_stats.created,
	     sim->adapts_stats.ncells.min,
	     sim->adapts_stats.ncells.mean,
	     sim->adapts_stats.ncells.stddev,
	     sim->adapts_stats.ncells.max,
	     sim->adapts_stats.ncells.n);
    if (sim->adapts_stats.cmax.n > 0)
      fprintf (GFS_OUTPUT (event)->file->fp,
	       "  Maximum cost\n"
	       "    min: %10.3e avg: %10.3e | %10.3e max: %10.3e n: %10d\n",
	       sim->adapts_stats.cmax.min,
	       sim->adapts_stats.cmax.mean,
	       sim->adapts_stats.cmax.stddev,
	       sim->adapts_stats.cmax.max,
	       sim->adapts_stats.cmax.n);
    gfs_adapt_stats_init (&sim->adapts_stats);
    return TRUE;
  }
  return FALSE;
}

static void gfs_output_adapt_stats_class_init (GfsOutputClass * klass)
{
  GFS_EVENT_CLASS (klass)->event = gfs_output_adapt_stats_event;
}

GfsOutputClass * gfs_output_adapt_stats_class (void)
{
  static GfsOutputClass * klass = NULL;

  if (klass == NULL) {
    GtsObjectClassInfo gfs_output_adapt_stats_info = {
      "GfsOutputAdaptStats",
      sizeof (GfsOutput),
      sizeof (GfsOutputClass),
      (GtsObjectClassInitFunc) gfs_output_adapt_stats_class_init,
      (GtsObjectInitFunc) NULL,
      (GtsArgSetFunc) NULL,
      (GtsArgGetFunc) NULL
    };
    klass = gts_object_class_new (GTS_OBJECT_CLASS (gfs_output_class ()),
				  &gfs_output_adapt_stats_info);
  }

  return klass;
}

/* GfsOutputTiming: Object */

static void timing_print (GtsRange * r, gdouble total, FILE * fp)
{
  fprintf (fp, 
	   "      min: %9.3f avg: %9.3f (%4.1f%%) | %7.3f max: %9.3f\n",
	   r->min,
	   r->mean, total > 0. ? 100.*r->sum/total : 0.,
	   r->stddev, 
	   r->max);	   
}

static void timer_print (gchar * name, GfsTimer * t, gpointer * data)
{
  FILE * fp = data[0];
  GfsDomain * domain = data[1];

  fprintf (fp, "  %s:\n", name);
  timing_print (&t->r, domain->timestep.sum, fp);
}

static gboolean timing_event (GfsEvent * event, GfsSimulation * sim)
{
  if ((* GFS_EVENT_CLASS (gfs_output_class())->event) (event, sim)) {
    GfsDomain * domain = GFS_DOMAIN (sim);
    FILE * fp = GFS_OUTPUT (event)->file->fp;

    if (domain->timestep.mean > 0.) {
      gpointer data[2];

      fprintf (fp,
	       "Timing summary: %u timesteps %.0f node.timestep/s\n"
	       "  timestep:\n"
	       "      min: %9.3f avg: %9.3f         | %7.3f max: %9.3f\n"
               "  domain size:\n"
	       "      min: %9.0f avg: %9.0f         | %7.0f max: %9.0f\n"
	       "  maximum number of variables: %d\n",
	       domain->timestep.n,
	       domain->size.mean/domain->timestep.mean,
	       domain->timestep.min,
	       domain->timestep.mean,
	       domain->timestep.stddev, 
	       domain->timestep.max,
	       domain->size.min,
	       domain->size.mean,
	       domain->size.stddev, 
	       domain->size.max,
	       gfs_domain_variables_number (domain));
      data[0] = fp;
      data[1] = domain;
      g_hash_table_foreach (domain->timers, (GHFunc) timer_print, data);
      if (domain->mpi_messages.n > 0)
	fprintf (fp,
		 "Message passing summary\n"
		 "  n: %10d size: %10.0f bytes\n",
		 domain->mpi_messages.n,
		 domain->mpi_messages.sum);
    }
    return TRUE;
  }
  return FALSE;
}

static void gfs_output_timing_class_init (GfsEventClass * klass)
{
  klass->event = timing_event;
}

GfsOutputClass * gfs_output_timing_class (void)
{
  static GfsOutputClass * klass = NULL;

  if (klass == NULL) {
    GtsObjectClassInfo gfs_output_timing_info = {
      "GfsOutputTiming",
      sizeof (GfsOutput),
      sizeof (GfsOutputClass),
      (GtsObjectClassInitFunc) gfs_output_timing_class_init,
      (GtsObjectInitFunc) NULL,
      (GtsArgSetFunc) NULL,
      (GtsArgGetFunc) NULL
    };
    klass = gts_object_class_new (GTS_OBJECT_CLASS (gfs_output_class ()),
				  &gfs_output_timing_info);
  }

  return klass;
}

/* GfsOutputBalance: Object */

static gboolean gfs_output_balance_event (GfsEvent * event, 
					  GfsSimulation * sim)
{
  if ((* GFS_EVENT_CLASS (GTS_OBJECT_CLASS (gfs_output_balance_class ())->parent_class)->event)
      (event, sim)) {
    GfsDomain * domain = GFS_DOMAIN (sim);
    FILE * fp = GFS_OUTPUT (event)->file->fp;
    GtsRange size, boundary, mpiwait;
    
    gfs_domain_stats_balance (domain, &size, &boundary, &mpiwait);
    fprintf (fp, 
	     "Balance summary: %u PE\n"
	     "  domain   min: %9.0f avg: %9.0f         | %7.0f max: %9.0f\n",
	     size.n,
	     size.min, size.mean, size.stddev, size.max);
    if (boundary.max > 0.)
      fprintf (fp, 
	       "  boundary min: %9.0f avg: %9.0f         | %7.0f max: %9.0f\n",
	       boundary.min, boundary.mean, boundary.stddev, boundary.max);
    if (mpiwait.max > 0.)
      fprintf (fp,
	       "  average timestep MPI wait time:\n"
	       "      min: %9.3f avg: %9.3f         | %7.3f max: %9.3f\n",
	       mpiwait.min, mpiwait.mean, mpiwait.stddev, mpiwait.max);
    return TRUE;
  }
  return FALSE;
}

static void gfs_output_balance_class_init (GfsOutputClass * klass)
{
  GFS_EVENT_CLASS (klass)->event = gfs_output_balance_event;
}

GfsOutputClass * gfs_output_balance_class (void)
{
  static GfsOutputClass * klass = NULL;

  if (klass == NULL) {
    GtsObjectClassInfo gfs_output_balance_info = {
      "GfsOutputBalance",
      sizeof (GfsOutput),
      sizeof (GfsOutputClass),
      (GtsObjectClassInitFunc) gfs_output_balance_class_init,
      (GtsObjectInitFunc) NULL,
      (GtsArgSetFunc) NULL,
      (GtsArgGetFunc) NULL
    };
    klass = gts_object_class_new (GTS_OBJECT_CLASS (gfs_output_class ()),
				  &gfs_output_balance_info);
  }

  return klass;
}

/* GfsOutputSolidForce: Object */

static gboolean gfs_output_solid_force_event (GfsEvent * event, 
					      GfsSimulation * sim)
{
  if ((* GFS_EVENT_CLASS (GTS_OBJECT_CLASS (gfs_output_solid_force_class ())->parent_class)->event)
      (event, sim) &&
      sim->advection_params.dt > 0.) {
    GfsDomain * domain = GFS_DOMAIN (sim);
    FILE * fp = GFS_OUTPUT (event)->file->fp;
    FttVector pf, vf, pm, vm;

    if (GFS_OUTPUT (event)->first_call)
      fputs ("# 1: T (2,3,4): Pressure force (5,6,7): Viscous force "
	     "(8,9,10): Pressure moment (11,12,13): Viscous moment\n", fp);
    
    gfs_domain_solid_force (domain, &pf, &vf, &pm, &vm);
    fprintf (fp, "%g %g %g %g %g %g %g %g %g %g %g %g %g\n",
	     sim->time.t,
	     pf.x, pf.y, pf.z,
	     vf.x, vf.y, vf.z,
	     pm.x, pm.y, pm.z,
	     vm.x, vm.y, vm.z);
    return TRUE;
  }
  return FALSE;
}

static void gfs_output_solid_force_class_init (GfsOutputClass * klass)
{
  GFS_EVENT_CLASS (klass)->event = gfs_output_solid_force_event;
}

GfsOutputClass * gfs_output_solid_force_class (void)
{
  static GfsOutputClass * klass = NULL;

  if (klass == NULL) {
    GtsObjectClassInfo gfs_output_solid_force_info = {
      "GfsOutputSolidForce",
      sizeof (GfsOutput),
      sizeof (GfsOutputClass),
      (GtsObjectClassInitFunc) gfs_output_solid_force_class_init,
      (GtsObjectInitFunc) NULL,
      (GtsArgSetFunc) NULL,
      (GtsArgGetFunc) NULL
    };
    klass = gts_object_class_new (GTS_OBJECT_CLASS (gfs_output_class ()),
				  &gfs_output_solid_force_info);
  }

  return klass;
}

/* GfsOutputLocation: Object */

static void gfs_output_location_destroy (GtsObject * object)
{
  g_array_free (GFS_OUTPUT_LOCATION (object)->p, TRUE);

  (* GTS_OBJECT_CLASS (gfs_output_location_class ())->parent_class->destroy) (object);
}

static gboolean vector_read (GtsFile * fp, FttVector * p)
{
  if (fp->type != GTS_INT && fp->type != GTS_FLOAT) {
    gts_file_error (fp, "expecting a number (p.x)");
    return FALSE;
  }
  p->x = atof (fp->token->str);
  gts_file_next_token (fp);

  if (fp->type != GTS_INT && fp->type != GTS_FLOAT) {
    gts_file_error (fp, "expecting a number (p.y)");
    return FALSE;
  }
  p->y = atof (fp->token->str);
  gts_file_next_token (fp);

  if (fp->type != GTS_INT && fp->type != GTS_FLOAT) {
    gts_file_error (fp, "expecting a number (p.z)");
    return FALSE;
  }
  p->z = atof (fp->token->str);
  gts_file_next_token (fp);
  return TRUE;
}

static void gfs_output_location_read (GtsObject ** o, GtsFile * fp)
{
  GfsOutputLocation * l = GFS_OUTPUT_LOCATION (*o);

  if (GTS_OBJECT_CLASS (gfs_output_location_class ())->parent_class->read)
    (* GTS_OBJECT_CLASS (gfs_output_location_class ())->parent_class->read) 
      (o, fp);
  if (fp->type == GTS_ERROR)
    return;

  if (fp->type == GTS_STRING) {
    FILE * fptr = fopen (fp->token->str, "r");
    GtsFile * fp1;

    if (fptr == NULL) {
      gts_file_error (fp, "cannot open file `%s'", fp->token->str);
      return;
    }
    fp1 = gts_file_new (fptr);
    while (fp1->type != GTS_NONE) {
      FttVector p;
      if (!vector_read (fp1, &p)) {
	gts_file_error (fp, "%s:%d:%d: %s", fp->token->str, fp1->line, fp1->pos, fp1->error);
	return;
      }
      g_array_append_val (l->p, p);
      while (fp1->type == '\n')
	gts_file_next_token (fp1);
    }
    gts_file_destroy (fp1);
    fclose (fptr);
    gts_file_next_token (fp);
  }
  else if (fp->type == '{') {
    fp->scope_max++;
    do
      gts_file_next_token (fp);
    while (fp->type == '\n');
    while (fp->type != GTS_NONE && fp->type != '}') {
      FttVector p;
      if (!vector_read (fp, &p))
	return;
      g_array_append_val (l->p, p);
      while (fp->type == '\n')
	gts_file_next_token (fp);
    }
    if (fp->type != '}') {
      gts_file_error (fp, "expecting a closing brace");
      return;
    }
    fp->scope_max--;
    gts_file_next_token (fp);
  }
  else {
    FttVector p;
    if (!vector_read (fp, &p))
      return;
    g_array_append_val (l->p, p);
  }
}

static void gfs_output_location_write (GtsObject * o, FILE * fp)
{
  GfsOutputLocation * l = GFS_OUTPUT_LOCATION (o);
  guint i;

  (* GTS_OBJECT_CLASS (gfs_output_location_class ())->parent_class->write) (o, fp);

  fputs (" {\n", fp);
  for (i = 0; i < l->p->len; i++) {
    FttVector p = g_array_index (l->p, FttVector, i);
    fprintf (fp, "%g %g %g\n", p.x, p.y, p.z);
  }
  fputc ('}', fp);
}

static gboolean gfs_output_location_event (GfsEvent * event, 
					   GfsSimulation * sim)
{
  if ((* GFS_EVENT_CLASS (GTS_OBJECT_CLASS (gfs_output_location_class ())->parent_class)->event)
      (event, sim)) {
    GfsDomain * domain = GFS_DOMAIN (sim);
    GfsOutputLocation * location = GFS_OUTPUT_LOCATION (event);
    FILE * fp = GFS_OUTPUT (event)->file->fp;
    guint i;

    if (GFS_OUTPUT (event)->first_call) {
      GSList * i = domain->variables;
      guint nv = 5;

      fputs ("# 1:T 2:X 3:Y 4:Z", fp);
      while (i) {
	if (GFS_VARIABLE1 (i->data)->name)
	  fprintf (fp, " %d:%s", nv++, GFS_VARIABLE1 (i->data)->name);
	i = i->next;
      }
      fputc ('\n', fp);
    }
    for (i = 0; i < location->p->len; i++) {
      FttVector p = g_array_index (location->p, FttVector, i);
      FttCell * cell = gfs_domain_locate (domain, p, -1);
      
      if (cell != NULL) {
	GSList * i = domain->variables;
	
	fprintf (fp, "%g %g %g %g", sim->time.t, p.x, p.y, p.z);
	while (i) {
	  if (GFS_VARIABLE1 (i->data)->name)
	    fprintf (fp, " %g", gfs_interpolate (cell, p, i->data));
	  i = i->next;
	}
	fputc ('\n', fp);
      }
    }
    fflush (fp);
    return TRUE;
  }
  return FALSE;
}

static void gfs_output_location_class_init (GfsOutputClass * klass)
{
  GFS_EVENT_CLASS (klass)->event = gfs_output_location_event;
  GTS_OBJECT_CLASS (klass)->destroy = gfs_output_location_destroy;
  GTS_OBJECT_CLASS (klass)->read = gfs_output_location_read;
  GTS_OBJECT_CLASS (klass)->write = gfs_output_location_write;
}

static void gfs_output_location_init (GfsOutputLocation * object)
{
  object->p = g_array_new (FALSE, FALSE, sizeof (FttVector));
}

GfsOutputClass * gfs_output_location_class (void)
{
  static GfsOutputClass * klass = NULL;

  if (klass == NULL) {
    GtsObjectClassInfo gfs_output_location_info = {
      "GfsOutputLocation",
      sizeof (GfsOutputLocation),
      sizeof (GfsOutputClass),
      (GtsObjectClassInitFunc) gfs_output_location_class_init,
      (GtsObjectInitFunc) gfs_output_location_init,
      (GtsArgSetFunc) NULL,
      (GtsArgGetFunc) NULL
    };
    klass = gts_object_class_new (GTS_OBJECT_CLASS (gfs_output_class ()),
				  &gfs_output_location_info);
  }

  return klass;
}

/* GfsOutputSimulation: Object */

static void output_simulation_destroy (GtsObject * object)
{
  GfsOutputSimulation * output = GFS_OUTPUT_SIMULATION (object);

  g_slist_free (output->var);

  (* GTS_OBJECT_CLASS (gfs_output_simulation_class ())->parent_class->destroy) (object);
}

static void write_text (FttCell * cell, GfsOutputSimulation * output)
{
  GSList * i = GFS_DOMAIN (gfs_object_simulation (output))->variables_io;
  FILE * fp = GFS_OUTPUT (output)->file->fp;
  FttVector p;

  gfs_cell_cm (cell, &p);
  fprintf (fp, "%g %g %g", p.x, p.y, p.z);
  while (i) {
    if (GFS_VARIABLE1 (i->data)->name)
      fprintf (fp, " %g", GFS_VARIABLE (cell, GFS_VARIABLE1 (i->data)->i));
    i = i->next;
  }
  fputc ('\n', fp);
}

static gboolean output_simulation_event (GfsEvent * event, GfsSimulation * sim)
{
  if ((* GFS_EVENT_CLASS (gfs_output_class())->event) (event, sim)) {
    GfsDomain * domain = GFS_DOMAIN (sim);
    GfsOutputSimulation * output = GFS_OUTPUT_SIMULATION (event);
    
    g_slist_free (domain->variables_io);
    domain->variables_io = output->var;
    domain->binary =       output->binary;
    sim->output_solid   =  output->solid;
    switch (output->format) {
    case GFS:
      gfs_simulation_write (sim,
			    output->max_depth,
			    GFS_OUTPUT (event)->file->fp);
      break;
    case GFS_TEXT: {
      FILE * fp = GFS_OUTPUT (event)->file->fp;
      GSList * i = domain->variables_io;
      guint nv = 4;

      fputs ("# 1:X 2:Y: 3:Z", fp);
      while (i) {
	if (GFS_VARIABLE1 (i->data)->name)
	  fprintf (fp, " %d:%s", nv++, GFS_VARIABLE1 (i->data)->name);
	i = i->next;
      }
      fputc ('\n', fp);
      gfs_domain_cell_traverse (domain, FTT_PRE_ORDER, FTT_TRAVERSE_LEAFS, -1,
				(FttCellTraverseFunc) write_text, event);
      break;
    }
    case GFS_VTK: {
      gfs_domain_write_vtk (domain, output->max_depth, domain->variables_io,
			    GFS_OUTPUT (event)->file->fp);
      break;
    }
    case GFS_TECPLOT: {
      gfs_domain_write_tecplot (domain, output->max_depth, domain->variables_io,
				GFS_OUTPUT (event)->file->fp);
      break;
    }
    default:
      g_assert_not_reached ();
    }
    domain->variables_io = NULL;
    domain->binary =       TRUE;
    sim->output_solid   =  TRUE;
    fflush (GFS_OUTPUT (event)->file->fp);
    return TRUE;
  }
  return FALSE;
}

static void output_simulation_write (GtsObject * o, FILE * fp)
{
  GfsOutputSimulation * output = GFS_OUTPUT_SIMULATION (o);
  GSList * i = output->var;

  (* GTS_OBJECT_CLASS (gfs_output_simulation_class ())->parent_class->write) (o, fp);

  fputs (" {", fp);
  if (output->max_depth != -1)
    fprintf (fp, " depth = %d", output->max_depth);
  if (i != NULL) {
    fprintf (fp, " variables = %s", GFS_VARIABLE1 (i->data)->name);
    i = i->next;
    while (i) {
      fprintf (fp, ",%s", GFS_VARIABLE1 (i->data)->name);
      i = i->next;
    }
  }
  if (!output->binary)
    fputs (" binary = 0", fp);
  if (!output->solid)
    fputs (" solid = 0", fp);
  switch (output->format) {
  case GFS_TEXT:    fputs (" format = text", fp);    break;
  case GFS_VTK:     fputs (" format = VTK", fp);     break;
  case GFS_TECPLOT: fputs (" format = Tecplot", fp); break;
  default: break;
  }
  fputs (" }", fp);
}

static void output_simulation_read (GtsObject ** o, GtsFile * fp)
{
  (* GTS_OBJECT_CLASS (gfs_output_simulation_class ())->parent_class->read) (o, fp);
  if (fp->type == GTS_ERROR)
    return;

  GfsOutputSimulation * output = GFS_OUTPUT_SIMULATION (*o);
  GfsDomain * domain = GFS_DOMAIN (gfs_object_simulation (output));
  if (output->var == NULL) {
    GSList * i = domain->variables;
    
    while (i) {
      if (GFS_VARIABLE1 (i->data)->name)
	output->var = g_slist_append (output->var, i->data);
      i = i->next;
    }
  }

  if (fp->type == '{') {
    GtsFileVariable var[] = {
      {GTS_INT,    "depth",    TRUE},
      {GTS_STRING, "variables",TRUE},
      {GTS_INT,    "binary",   TRUE},
      {GTS_INT,    "solid",    TRUE},
      {GTS_STRING, "format",   TRUE},
      {GTS_NONE}
    };
    gchar * variables = NULL, * format = NULL;

    var[0].data = &output->max_depth;
    var[1].data = &variables;
    var[2].data = &output->binary;
    var[3].data = &output->solid;
    var[4].data = &format;
    gts_file_assign_variables (fp, var);
    if (fp->type == GTS_ERROR) {
      g_free (variables);
      return;
    }

    if (variables != NULL) {
      gchar * error = NULL;
      GSList * vars = gfs_variables_from_list (domain->variables, variables, &error);

      if (vars == NULL) {
	gts_file_variable_error (fp, var, "variables",
				 "unknown variable `%s'", error);
	g_free (variables);
	return;
      }
      if (output->var)
	g_slist_free (output->var);
      output->var = vars;
      g_free (variables);
    }

    if (format != NULL) {
      if (!strcmp (format, "gfs"))
	output->format = GFS;
      else if (!strcmp (format, "text"))
	output->format = GFS_TEXT;
      else if (!strcmp (format, "VTK"))
	output->format = GFS_VTK;
      else if (!strcmp (format, "Tecplot"))
	output->format = GFS_TECPLOT;
      else {
	gts_file_variable_error (fp, var, "format",
				 "unknown format `%s'", format);
	g_free (format);
	return;
      }
      g_free (format);
    }
  }
}

static void gfs_output_simulation_class_init (GfsEventClass * klass)
{
  klass->event = output_simulation_event;
  GTS_OBJECT_CLASS (klass)->destroy = output_simulation_destroy;
  GTS_OBJECT_CLASS (klass)->read = output_simulation_read;
  GTS_OBJECT_CLASS (klass)->write = output_simulation_write;
}

static void gfs_output_simulation_init (GfsOutputSimulation * object)
{
  object->max_depth = -1;
  object->var = NULL;
  object->binary = 1;
  object->solid = 1;
  object->format = GFS;
}

GfsOutputClass * gfs_output_simulation_class (void)
{
  static GfsOutputClass * klass = NULL;

  if (klass == NULL) {
    GtsObjectClassInfo gfs_output_simulation_info = {
      "GfsOutputSimulation",
      sizeof (GfsOutputSimulation),
      sizeof (GfsOutputClass),
      (GtsObjectClassInitFunc) gfs_output_simulation_class_init,
      (GtsObjectInitFunc) gfs_output_simulation_init,
      (GtsArgSetFunc) NULL,
      (GtsArgGetFunc) NULL
    };
    klass = gts_object_class_new (GTS_OBJECT_CLASS (gfs_output_class ()),
				  &gfs_output_simulation_info);
  }

  return klass;
}

/* GfsOutputBoundaries: Object */

static gboolean output_boundaries_event (GfsEvent * event, GfsSimulation * sim)
{
  if ((* GFS_EVENT_CLASS (gfs_output_class())->event) (event, sim)) {
    GfsDomain * domain = GFS_DOMAIN (sim);
    FILE * fp = GFS_OUTPUT (event)->file->fp;
    
    gfs_draw_refined_boundaries (domain, fp);
    gfs_draw_solid_boundaries (domain, fp);
    gfs_draw_boundary_conditions (domain, fp);
    fflush (fp);
    return TRUE;
  }
  return FALSE;
}

static void gfs_output_boundaries_class_init (GfsEventClass * klass)
{
  klass->event = output_boundaries_event;
}

GfsOutputClass * gfs_output_boundaries_class (void)
{
  static GfsOutputClass * klass = NULL;

  if (klass == NULL) {
    GtsObjectClassInfo gfs_output_boundaries_info = {
      "GfsOutputBoundaries",
      sizeof (GfsOutput),
      sizeof (GfsOutputClass),
      (GtsObjectClassInitFunc) gfs_output_boundaries_class_init,
      (GtsObjectInitFunc) NULL,
      (GtsArgSetFunc) NULL,
      (GtsArgGetFunc) NULL
    };
    klass = gts_object_class_new (GTS_OBJECT_CLASS (gfs_output_class ()),
				  &gfs_output_boundaries_info);
  }

  return klass;
}

/* GfsOutputScalar: Object */

static void gfs_output_scalar_destroy (GtsObject * o)
{
  GfsOutputScalar * output = GFS_OUTPUT_SCALAR (o);

  if (output->box)
    gts_object_destroy (GTS_OBJECT (output->box));
  gts_object_destroy (GTS_OBJECT (output->f));
  g_free (output->name);

  (* GTS_OBJECT_CLASS (gfs_output_scalar_class ())->parent_class->destroy) (o);
}

static void gfs_output_scalar_read (GtsObject ** o, GtsFile * fp)
{
  GfsOutputScalar * output;

  if (GTS_OBJECT_CLASS (gfs_output_scalar_class ())->parent_class->read)
    (* GTS_OBJECT_CLASS (gfs_output_scalar_class ())->parent_class->read) 
      (o, fp);
  if (fp->type == GTS_ERROR)
    return;

  output = GFS_OUTPUT_SCALAR (*o);
  output->autoscale = TRUE;

  if (fp->type != '{') {
    gts_file_error (fp, "expecting an opening brace");
    return;
  }
  fp->scope_max++;
  gts_file_next_token (fp);

  while (fp->type != GTS_ERROR && fp->type != '}') {
    if (fp->type == '\n') {
      gts_file_next_token (fp);
      continue;
    }
    if (fp->type != GTS_STRING) {
      gts_file_error (fp, "expecting a keyword");
      return;
    }
    else if (!strcmp (fp->token->str, "v")) {
      gts_file_next_token (fp);
      if (fp->type != '=') {
	gts_file_error (fp, "expecting '='");
	return;
      }
      gts_file_next_token (fp);
      gfs_function_read (output->f, gfs_object_simulation (*o), fp);
      output->name = gfs_function_description (output->f, TRUE);
    }
    else if (!strcmp (fp->token->str, "min")) {
      gts_file_next_token (fp);
      if (fp->type != '=') {
	gts_file_error (fp, "expecting '='");
	return;
      }
      gts_file_next_token (fp);
      output->min = gfs_read_constant (fp, gfs_object_simulation (*o));
      if (fp->type == GTS_ERROR)
	return;
      if (output->min > output->max) {
	gts_file_error (fp, "min `%g' must be smaller than or equal to max `%g'", 
			output->min, output->max);
	return;
      }
      output->autoscale = FALSE;
    }
    else if (!strcmp (fp->token->str, "max")) {
      gts_file_next_token (fp);
      if (fp->type != '=') {
	gts_file_error (fp, "expecting '='");
	return;
      }
      gts_file_next_token (fp);
      output->max = gfs_read_constant (fp, gfs_object_simulation (*o));
      if (fp->type == GTS_ERROR)
	return;
      if (output->max < output->min) {
	gts_file_error (fp, "max `%g' must be larger than or equal to min `%g'", 
			output->max, output->min);
	return;
      }
      output->autoscale = FALSE;
    }
    else if (!strcmp (fp->token->str, "maxlevel")) {
      gts_file_next_token (fp);
      if (fp->type != '=') {
	gts_file_error (fp, "expecting '='");
	return;
      }
      gts_file_next_token (fp);
      if (fp->type != GTS_INT) {
	gts_file_error (fp, "expecting an integer (maxlevel)");
	return;
      }
      output->maxlevel = atoi (fp->token->str);
      gts_file_next_token (fp);
    }
    else if (!strcmp (fp->token->str, "box")) {
      gchar * box, * s;

      gts_file_next_token (fp);
      if (fp->type != '=') {
	gts_file_error (fp, "expecting '='");
	return;
      }
      gts_file_next_token (fp);
      if (fp->type != GTS_STRING) {
	gts_file_error (fp, "expecting a string (box)");
	return;
      }
      box = g_strdup (fp->token->str);
      s = strtok (box, ",");
      output->box = GTS_BBOX (gts_object_new (GTS_OBJECT_CLASS (gts_bbox_class ())));
      if (s == NULL) {
	gts_file_error (fp, "expecting a number (x1)");
	g_free (box);
	return;
      }
      output->box->x1 = atof (s);
      s = strtok (NULL, ",");
      if (s == NULL) {
	gts_file_error (fp, "expecting a number (y1)");
	g_free (box);
	return;
      }
      output->box->y1 = atof (s);
      s = strtok (NULL, ",");
#if (!FTT_2D)
      if (s == NULL) {
	gts_file_error (fp, "expecting a number (z1)");
	g_free (box);
	return;
      }
      output->box->z1 = atof (s);
      s = strtok (NULL, ",");
#endif /* 3D */
      if (s == NULL) {
	gts_file_error (fp, "expecting a number (x2)");
	g_free (box);
	return;
      }
      output->box->x2 = atof (s);
      if (output->box->x2 < output->box->x1) {
	gts_file_error (fp, "x2 must be larger than x1");
	g_free (box);
	return;
      }
      s = strtok (NULL, ",");
      if (s == NULL) {
	gts_file_error (fp, "expecting a number (y2)");
	g_free (box);
	return;
      }
      output->box->y2 = atof (s);
      if (output->box->y2 < output->box->y1) {
	gts_file_error (fp, "y2 must be larger than y1");
	g_free (box);
	return;
      }
#if (!FTT_2D)
      s = strtok (NULL, ",");
      if (s == NULL) {
	gts_file_error (fp, "expecting a number (z2)");
	g_free (box);
	return;
      }
      output->box->z2 = atof (s);
      if (output->box->z2 < output->box->z1) {
	gts_file_error (fp, "z2 must be larger than z1");
	g_free (box);
	return;
      }
#endif /* 3D */
      g_free (box);
      gts_file_next_token (fp);
    }
    else {
      gts_file_error (fp, "unknown keyword `%s'", fp->token->str);
      return;
    }
  }
  if (fp->type == GTS_ERROR)
    return;
  if (fp->type != '}') {
    gts_file_error (fp, "expecting a closing brace");
    return;
  }
  fp->scope_max--;
  gts_file_next_token (fp);
}

static void gfs_output_scalar_write (GtsObject * o, FILE * fp)
{
  GfsOutputScalar * output = GFS_OUTPUT_SCALAR (o);

  if (GTS_OBJECT_CLASS (gfs_output_scalar_class ())->parent_class->write)
    (* GTS_OBJECT_CLASS (gfs_output_scalar_class ())->parent_class->write) 
      (o, fp);

  fputs (" { v = ", fp);
  gfs_function_write (output->f, fp);
  if (output->maxlevel >= 0)
    fprintf (fp, " maxlevel = %d", output->maxlevel);
  if (output->box != NULL)
#if FTT_2D
    fprintf (fp, " box = %g,%g,%g,%g", 
	     output->box->x1, output->box->y1, output->box->x2, output->box->y2);
#else  /* 3D */
    fprintf (fp, " box = %g,%g,%g,%g,%g,%g",
	     output->box->x1, output->box->y1, output->box->z1,
	     output->box->x2, output->box->y2, output->box->z2);
#endif /* 3D */
  if (!output->autoscale)
    fprintf (fp, " min = %g max = %g }", output->min, output->max);
  else
    fputs (" }", fp);
}

static void update_v (FttCell * cell, GfsOutputScalar * output)
{
  GFS_VARIABLE (cell, output->v->i) = gfs_function_value (output->f, cell);
}

static gboolean gfs_output_scalar_event (GfsEvent * event,
					 GfsSimulation * sim)
{
  if ((* GFS_EVENT_CLASS (GTS_OBJECT_CLASS (gfs_output_scalar_class ())->parent_class)->event)
      (event, sim)) {
    GfsOutputScalar * output = GFS_OUTPUT_SCALAR (event);
    GfsDomain * domain = GFS_DOMAIN (sim);

    if (!(output->v = gfs_function_get_variable (output->f))) {
      output->v = gfs_variable_new (gfs_variable_class (), domain, NULL, NULL);
      gfs_domain_cell_traverse (domain,
				FTT_PRE_ORDER, FTT_TRAVERSE_LEAFS, -1,
				(FttCellTraverseFunc) update_v, output);
    }
    if (output->maxlevel >= 0)
        gfs_domain_cell_traverse (domain,
				  FTT_POST_ORDER, FTT_TRAVERSE_NON_LEAFS, -1,
				  (FttCellTraverseFunc) output->v->fine_coarse,
				  output->v);
    if (output->autoscale) {
      GtsRange stats = gfs_domain_stats_variable (domain, output->v, 
	     FTT_TRAVERSE_LEAFS|FTT_TRAVERSE_LEVEL, output->maxlevel);

      output->min = stats.min;
      output->max = stats.max;
    }
    return TRUE;
  }
  return FALSE;
}

static void gfs_output_scalar_post_event (GfsEvent * event,
					  GfsSimulation * sim)
{
  GfsOutputScalar * output = GFS_OUTPUT_SCALAR (event);

  if (!gfs_function_get_variable (output->f)) {
    gts_object_destroy (GTS_OBJECT (output->v));
    output->v = NULL;
  }
}

static void gfs_output_scalar_class_init (GfsOutputClass * klass)
{
  GFS_EVENT_CLASS (klass)->event = gfs_output_scalar_event;
  GFS_EVENT_CLASS (klass)->post_event = gfs_output_scalar_post_event;
  GTS_OBJECT_CLASS (klass)->read = gfs_output_scalar_read;
  GTS_OBJECT_CLASS (klass)->write = gfs_output_scalar_write;
  GTS_OBJECT_CLASS (klass)->destroy = gfs_output_scalar_destroy;
}

static void gfs_output_scalar_init (GfsOutputScalar * object)
{
  object->f = gfs_function_new (gfs_function_class (), 0.);
  object->min = -G_MAXDOUBLE;
  object->max =  G_MAXDOUBLE;
  object->autoscale = TRUE;
  object->maxlevel = -1;
  object->box = NULL;
}

GfsOutputClass * gfs_output_scalar_class (void)
{
  static GfsOutputClass * klass = NULL;

  if (klass == NULL) {
    GtsObjectClassInfo gfs_output_scalar_info = {
      "GfsOutputScalar",
      sizeof (GfsOutputScalar),
      sizeof (GfsOutputClass),
      (GtsObjectClassInitFunc) gfs_output_scalar_class_init,
      (GtsObjectInitFunc) gfs_output_scalar_init,
      (GtsArgSetFunc) NULL,
      (GtsArgGetFunc) NULL
    };
    klass = gts_object_class_new (GTS_OBJECT_CLASS (gfs_output_class ()),
				  &gfs_output_scalar_info);
  }

  return klass;
}

/* GfsOutputScalarNorm: Object */

static gboolean gfs_output_scalar_norm_event (GfsEvent * event, 
					      GfsSimulation * sim)
{
  if ((* GFS_EVENT_CLASS (GTS_OBJECT_CLASS (gfs_output_scalar_norm_class ())->parent_class)->event)
      (event, sim)) {
    GfsOutputScalar * output = GFS_OUTPUT_SCALAR (event);
    GfsNorm norm = gfs_domain_norm_variable (GFS_DOMAIN (sim), 
					     output->v, NULL,
					     FTT_TRAVERSE_LEAFS|FTT_TRAVERSE_LEVEL, 
					     output->maxlevel);

    fprintf (GFS_OUTPUT (event)->file->fp, 
	     "%s time: %g first: % 10.3e second: % 10.3e infty: % 10.3e\n",
	     output->name,
	     sim->time.t,
	     norm.first, norm.second, norm.infty);
    return TRUE;
  }
  return FALSE;
}

static void gfs_output_scalar_norm_class_init (GfsOutputClass * klass)
{
  GFS_EVENT_CLASS (klass)->event = gfs_output_scalar_norm_event;
}

GfsOutputClass * gfs_output_scalar_norm_class (void)
{
  static GfsOutputClass * klass = NULL;

  if (klass == NULL) {
    GtsObjectClassInfo gfs_output_scalar_norm_info = {
      "GfsOutputScalarNorm",
      sizeof (GfsOutputScalar),
      sizeof (GfsOutputClass),
      (GtsObjectClassInitFunc) gfs_output_scalar_norm_class_init,
      (GtsObjectInitFunc) NULL,
      (GtsArgSetFunc) NULL,
      (GtsArgGetFunc) NULL
    };
    klass = gts_object_class_new (GTS_OBJECT_CLASS (gfs_output_scalar_class ()),
				  &gfs_output_scalar_norm_info);
  }

  return klass;
}

/* GfsOutputScalarStats: Object */

static gboolean gfs_output_scalar_stats_event (GfsEvent * event, 
					     GfsSimulation * sim)
{
  if ((* GFS_EVENT_CLASS (GTS_OBJECT_CLASS (gfs_output_scalar_stats_class ())->parent_class)->event)
      (event, sim)) {
    GfsOutputScalar * output = GFS_OUTPUT_SCALAR (event);
    GtsRange stats = gfs_domain_stats_variable (GFS_DOMAIN (sim), 
						output->v,
						FTT_TRAVERSE_LEAFS|FTT_TRAVERSE_LEVEL, 
						output->maxlevel);

    fprintf (GFS_OUTPUT (event)->file->fp, 
	     "%s time: %g min: %10.3e avg: %10.3e | %10.3e max: %10.3e\n",
	     output->name, sim->time.t,
	     stats.min, stats.mean, stats.stddev, stats.max);
    return TRUE;
  }
  return FALSE;
}

static void gfs_output_scalar_stats_class_init (GfsOutputClass * klass)
{
  GFS_EVENT_CLASS (klass)->event = gfs_output_scalar_stats_event;
}

GfsOutputClass * gfs_output_scalar_stats_class (void)
{
  static GfsOutputClass * klass = NULL;

  if (klass == NULL) {
    GtsObjectClassInfo gfs_output_scalar_stats_info = {
      "GfsOutputScalarStats",
      sizeof (GfsOutputScalar),
      sizeof (GfsOutputClass),
      (GtsObjectClassInitFunc) gfs_output_scalar_stats_class_init,
      (GtsObjectInitFunc) NULL,
      (GtsArgSetFunc) NULL,
      (GtsArgGetFunc) NULL
    };
    klass = gts_object_class_new (GTS_OBJECT_CLASS (gfs_output_scalar_class ()),
				  &gfs_output_scalar_stats_info);
  }

  return klass;
}

/* GfsOutputScalarSum: Object */

static void add (FttCell * cell, gpointer * data)
{
  gdouble vol = gfs_cell_volume (cell);
  GfsVariable * v = data[0];
  gdouble * sum = data[1];

  *sum += vol*GFS_VARIABLE (cell, v->i);
}

static gboolean gfs_output_scalar_sum_event (GfsEvent * event, 
					     GfsSimulation * sim)
{
  if ((* GFS_EVENT_CLASS (GTS_OBJECT_CLASS (gfs_output_scalar_sum_class ())->parent_class)->event)
      (event, sim)) {
    GfsOutputScalar * output = GFS_OUTPUT_SCALAR (event);
    gpointer data[2];
    gdouble sum = 0.;

    data[0] = output->v;
    data[1] = &sum;
    gfs_domain_cell_traverse (GFS_DOMAIN (sim),
			      FTT_PRE_ORDER, 
			      FTT_TRAVERSE_LEAFS|FTT_TRAVERSE_LEVEL,
			      output->maxlevel,
			      (FttCellTraverseFunc) add, data);
    fprintf (GFS_OUTPUT (event)->file->fp, 
	     "%s time: %g sum: % 15.6e\n", output->name, sim->time.t, sum);
    return TRUE;
  }
  return FALSE;
}

static void gfs_output_scalar_sum_class_init (GfsOutputClass * klass)
{
  GFS_EVENT_CLASS (klass)->event = gfs_output_scalar_sum_event;
}

GfsOutputClass * gfs_output_scalar_sum_class (void)
{
  static GfsOutputClass * klass = NULL;

  if (klass == NULL) {
    GtsObjectClassInfo gfs_output_scalar_sum_info = {
      "GfsOutputScalarSum",
      sizeof (GfsOutputScalar),
      sizeof (GfsOutputClass),
      (GtsObjectClassInitFunc) gfs_output_scalar_sum_class_init,
      (GtsObjectInitFunc) NULL,
      (GtsArgSetFunc) NULL,
      (GtsArgGetFunc) NULL
    };
    klass = gts_object_class_new (GTS_OBJECT_CLASS (gfs_output_scalar_class ()),
				  &gfs_output_scalar_sum_info);
  }

  return klass;
}

/* GfsOutputScalarMaxima: Object */

static void gfs_output_scalar_maxima_destroy (GtsObject * o)
{
  guint i;

  for (i = 0; i < 4; i++)
    g_free (GFS_OUTPUT_SCALAR_MAXIMA (o)->m[i]);

  (* GTS_OBJECT_CLASS (gfs_output_scalar_maxima_class ())->parent_class->destroy) (o);
}

static void gfs_output_scalar_maxima_read (GtsObject ** o, GtsFile * fp)
{
  GfsOutputScalarMaxima * m;
  guint i;

  (* GTS_OBJECT_CLASS (gfs_output_scalar_maxima_class ())->parent_class->read) (o, fp);
  if (fp->type == GTS_ERROR)
    return;

  if (fp->type != GTS_INT) {
    gts_file_error (fp, "expecting an integer (N)");
    return;
  }
  m = GFS_OUTPUT_SCALAR_MAXIMA (*o);
  m->N = atoi (fp->token->str);
  gts_file_next_token (fp);

  for (i = 0; i < 4; i++)
    m->m[i] = g_malloc (sizeof (gdouble)*m->N);
}

static void gfs_output_scalar_maxima_write (GtsObject * o, FILE * fp)
{
  (* GTS_OBJECT_CLASS (gfs_output_scalar_maxima_class ())->parent_class->write) (o, fp);
  fprintf (fp, " %d", GFS_OUTPUT_SCALAR_MAXIMA (o)->N);
}

static void maxima (FttCell * cell, GfsOutputScalarMaxima * m)
{
  guint i;

  for (i = 0; i < m->N; i++) {
    gdouble v = GFS_VARIABLE (cell, GFS_OUTPUT_SCALAR (m)->v->i);

    if (v > m->m[3][i]) {
      FttVector p;

      gfs_cell_cm (cell, &p);
      m->m[0][i] = p.x; m->m[1][i] = p.y; m->m[2][i] = p.z;
      m->m[3][i] = v;
      return;
    }
  }
}

static gboolean gfs_output_scalar_maxima_event (GfsEvent * event, 
						GfsSimulation * sim)
{
  if ((* GFS_EVENT_CLASS (GTS_OBJECT_CLASS (gfs_output_scalar_maxima_class ())->parent_class)->event)
      (event, sim)) {
    GfsOutputScalar * output = GFS_OUTPUT_SCALAR (event);
    GfsOutputScalarMaxima * m = GFS_OUTPUT_SCALAR_MAXIMA (event);
    guint i;

    for (i = 0; i < m->N; i++)
      m->m[3][i] = -G_MAXDOUBLE;

    gfs_domain_cell_traverse (GFS_DOMAIN (sim),
			      FTT_PRE_ORDER, 
			      FTT_TRAVERSE_LEAFS|FTT_TRAVERSE_LEVEL,
			      output->maxlevel,
			      (FttCellTraverseFunc) maxima, m);
    for (i = 0; i < m->N; i++)
      fprintf (GFS_OUTPUT (event)->file->fp, 
	       "%s time: %g #: %d x: %g y: %g z: %g value: %g\n", 
	       output->name, sim->time.t, i,
	       m->m[0][i], m->m[1][i], m->m[2][i],
	       m->m[3][i]);
    return TRUE;
  }
  return FALSE;
}

static void gfs_output_scalar_maxima_class_init (GfsOutputClass * klass)
{
  GTS_OBJECT_CLASS (klass)->destroy = gfs_output_scalar_maxima_destroy;
  GTS_OBJECT_CLASS (klass)->read = gfs_output_scalar_maxima_read;
  GTS_OBJECT_CLASS (klass)->write = gfs_output_scalar_maxima_write;
  GFS_EVENT_CLASS (klass)->event = gfs_output_scalar_maxima_event;
}

GfsOutputClass * gfs_output_scalar_maxima_class (void)
{
  static GfsOutputClass * klass = NULL;

  if (klass == NULL) {
    GtsObjectClassInfo gfs_output_scalar_maxima_info = {
      "GfsOutputScalarMaxima",
      sizeof (GfsOutputScalarMaxima),
      sizeof (GfsOutputClass),
      (GtsObjectClassInitFunc) gfs_output_scalar_maxima_class_init,
      (GtsObjectInitFunc) NULL,
      (GtsArgSetFunc) NULL,
      (GtsArgGetFunc) NULL
    };
    klass = gts_object_class_new (GTS_OBJECT_CLASS (gfs_output_scalar_class ()),
				  &gfs_output_scalar_maxima_info);
  }

  return klass;
}

/* GfsOutputScalarHistogram: Object */

static void gfs_output_scalar_histogram_destroy (GtsObject * o)
{
  GfsOutputScalarHistogram * output = GFS_OUTPUT_SCALAR_HISTOGRAM (o);

  g_free (output->x);
  g_free (output->w);
  if (output->wf)
    gts_object_destroy (GTS_OBJECT (output->wf));
  if (output->yf) {
    gts_object_destroy (GTS_OBJECT (output->yf));
    g_free (output->y);
  }

  (* GTS_OBJECT_CLASS (gfs_output_scalar_histogram_class ())->parent_class->destroy) (o);
}

static void gfs_output_scalar_histogram_read (GtsObject ** o, GtsFile * fp)
{
  GfsOutputScalarHistogram * output;

  (* GTS_OBJECT_CLASS (gfs_output_scalar_histogram_class ())->parent_class->read) (o, fp);
  if (fp->type == GTS_ERROR)
    return;

  output = GFS_OUTPUT_SCALAR_HISTOGRAM (*o);
  if (fp->type != '{') {
    gts_file_error (fp, "expecting an opening brace");
    return;
  }
  fp->scope_max++;
  gts_file_next_token (fp);

  while (fp->type != GTS_ERROR && fp->type != '}') {
    if (fp->type == '\n') {
      gts_file_next_token (fp);
      continue;
    }
    if (fp->type != GTS_STRING) {
      gts_file_error (fp, "expecting a keyword");
      return;
    }
    else if (!strcmp (fp->token->str, "n")) {
      gts_file_next_token (fp);
      if (fp->type != '=') {
	gts_file_error (fp, "expecting '='");
	return;
      }
      gts_file_next_token (fp);
      if (fp->type != GTS_INT) {
	gts_file_error (fp, "expecting a number (n)");
	return;
      }
      output->n = atoi (fp->token->str);
      if (output->n <= 0) {
	gts_file_error (fp, "n `%d' must be strictly positive", output->n);
	return;
      }
      gts_file_next_token (fp);
    }
    else if (!strcmp (fp->token->str, "w")) {
      gts_file_next_token (fp);
      if (fp->type != '=') {
	gts_file_error (fp, "expecting '='");
	return;
      }
      gts_file_next_token (fp);
      output->wf = gfs_function_new (gfs_function_class (), 0.);
      gfs_function_read (output->wf, gfs_object_simulation (*o), fp);
    }
    else if (!strcmp (fp->token->str, "y")) {
      gts_file_next_token (fp);
      if (fp->type != '=') {
	gts_file_error (fp, "expecting '='");
	return;
      }
      gts_file_next_token (fp);
      output->yf = gfs_function_new (gfs_function_class (), 0.);
      gfs_function_read (output->yf, gfs_object_simulation (*o), fp);
    }
    else {
      gts_file_error (fp, "unknown keyword `%s'", fp->token->str);
      return;
    }
  }
  if (fp->type == GTS_ERROR)
    return;
  if (fp->type != '}') {
    gts_file_error (fp, "expecting a closing brace");
    return;
  }
  fp->scope_max--;
  gts_file_next_token (fp);

  output->x = g_malloc0 (output->n*sizeof (gdouble));
  output->w = g_malloc0 (output->n*sizeof (gdouble));
  if (output->yf)
    output->y = g_malloc0 (output->n*sizeof (gdouble));
}

static void gfs_output_scalar_histogram_write (GtsObject * o, FILE * fp)
{
  GfsOutputScalarHistogram * output = GFS_OUTPUT_SCALAR_HISTOGRAM (o);

  (* GTS_OBJECT_CLASS (gfs_output_scalar_histogram_class ())->parent_class->write) (o, fp);

  fprintf (fp, " { n = %d", output->n);
  if (output->wf) {
    fputs (" w = ", fp);
    gfs_function_write (output->wf, fp);
  }
  if (output->yf) {
    fputs (" y = ", fp);
    gfs_function_write (output->yf, fp);
  }
  fputs (" }", fp);
}

static void update_histogram (FttCell * cell, GfsOutputScalar * h)
{
  GfsOutputScalarHistogram * hi = GFS_OUTPUT_SCALAR_HISTOGRAM (h);
  gdouble v = GFS_VARIABLE (cell, h->v->i);
  gint i = (v - h->min)/(h->max - h->min)*hi->n;

  if (i >= 0 && i < hi->n) {
    gdouble w = hi->dt;

    if (hi->wf)
      w *= gfs_function_value (hi->wf, cell);
    else
      w *= gfs_cell_volume (cell);

    hi->W += w;
    hi->w[i] += w;
    hi->x[i] += v*w;
    if (hi->yf)
      hi->y[i] += w*gfs_function_value (hi->yf, cell);
  }
}

static gboolean gfs_output_scalar_histogram_event (GfsEvent * event,
						   GfsSimulation * sim)
{
  if ((* GFS_EVENT_CLASS (GTS_OBJECT_CLASS (gfs_output_scalar_histogram_class ())->parent_class)->event)
      (event, sim)) {
    GfsOutputScalarHistogram * h = GFS_OUTPUT_SCALAR_HISTOGRAM (event);

    if (gfs_event_is_repetitive (event))
      h->dt = h->last >= 0. ? sim->time.t - h->last : 0.;
    else
      h->dt = 1.;

    if (h->dt > 0.) {
      GfsOutput * output = GFS_OUTPUT (event);
      guint i;

      gfs_domain_cell_traverse (GFS_DOMAIN (sim), FTT_PRE_ORDER, 
				FTT_TRAVERSE_LEAFS|FTT_TRAVERSE_LEVEL, 
				GFS_OUTPUT_SCALAR (output)->maxlevel,
				(FttCellTraverseFunc) update_histogram, output);

      if (output->file && !output->dynamic)
	output->file->fp = freopen (output->format, "w", output->file->fp);
      for (i = 0; i < h->n; i++)
	if (h->w[i] > 0.) {
	  fprintf (output->file->fp, "%g %g", h->x[i]/h->w[i], h->w[i]/h->W);
	  if (h->yf)
	    fprintf (output->file->fp, " %g", h->y[i]/h->w[i]);
	  fputc ('\n', output->file->fp);
	}
      if (output->file && !output->dynamic)
	fflush (output->file->fp);
    }
    h->last = sim->time.t;
    return TRUE;
  }
  return FALSE;
}

static void gfs_output_scalar_histogram_class_init (GfsOutputClass * klass)
{
  GFS_EVENT_CLASS (klass)->event = gfs_output_scalar_histogram_event;
  GTS_OBJECT_CLASS (klass)->read = gfs_output_scalar_histogram_read;
  GTS_OBJECT_CLASS (klass)->write = gfs_output_scalar_histogram_write;
  GTS_OBJECT_CLASS (klass)->destroy = gfs_output_scalar_histogram_destroy;
}

static void gfs_output_scalar_histogram_init (GfsOutputScalarHistogram * object)
{
  GFS_OUTPUT_SCALAR (object)->min = -1.;
  GFS_OUTPUT_SCALAR (object)->max =  1.;
  GFS_OUTPUT_SCALAR (object)->autoscale = FALSE;
  object->n = 100;
  object->W = 0.;
  object->last = -1.;
}

GfsOutputClass * gfs_output_scalar_histogram_class (void)
{
  static GfsOutputClass * klass = NULL;

  if (klass == NULL) {
    GtsObjectClassInfo gfs_output_scalar_histogram_info = {
      "GfsOutputScalarHistogram",
      sizeof (GfsOutputScalarHistogram),
      sizeof (GfsOutputClass),
      (GtsObjectClassInitFunc) gfs_output_scalar_histogram_class_init,
      (GtsObjectInitFunc) gfs_output_scalar_histogram_init,
      (GtsArgSetFunc) NULL,
      (GtsArgGetFunc) NULL
    };
    klass = gts_object_class_new (GTS_OBJECT_CLASS (gfs_output_scalar_class ()),
				  &gfs_output_scalar_histogram_info);
  }

  return klass;
}

/* GfsOutputDropletSums: Object */

static void gfs_output_droplet_sums_destroy (GtsObject * object)
{
  GfsOutputDropletSums * d = GFS_OUTPUT_DROPLET_SUMS (object);
  gts_object_destroy (GTS_OBJECT (d->c));
  if (d->tag)
    gts_object_destroy (GTS_OBJECT (d->tag));

  (* GTS_OBJECT_CLASS (gfs_output_droplet_sums_class ())->parent_class->destroy) (object);
}

static void gfs_output_droplet_sums_read (GtsObject ** o, GtsFile * fp)
{
  (* GTS_OBJECT_CLASS (gfs_output_droplet_sums_class ())->parent_class->read) (o, fp);
  if (fp->type == GTS_ERROR)
    return;

  GfsOutputDropletSums * d = GFS_OUTPUT_DROPLET_SUMS (*o);
  GfsDomain * domain = GFS_DOMAIN (gfs_object_simulation (*o));
  gfs_function_read (d->c, domain, fp);
  if (fp->type == GTS_ERROR)
    return;

  if (fp->type == GTS_STRING) {
    if (!(d->tag = gfs_domain_get_or_add_variable (domain, fp->token->str, "Droplet index"))) {
      gts_file_error (fp, "`%s' is a reserved variable name", fp->token->str);
      return;
    }
    gts_file_next_token (fp);
  }
}

static void gfs_output_droplet_sums_write (GtsObject * o, FILE * fp)
{
  GfsOutputDropletSums * d = GFS_OUTPUT_DROPLET_SUMS (o);

  (* GTS_OBJECT_CLASS (gfs_output_droplet_sums_class ())->parent_class->write) (o, fp);

  gfs_function_write (d->c, fp);
  if (d->tag)
    fprintf (fp, " %s", d->tag->name);
}

typedef struct {
  GfsVariable * s, * c, * tag;
  double * v;
  guint n;
  GfsFunction * fc;
} DropSumsPar;

static void droplet_sums (FttCell * cell, DropSumsPar * p)
{
  guint i = GFS_VALUE (cell, p->tag);
  if (i > 0)
    p->v[i - 1] += GFS_VALUE (cell, p->s);
}

static void compute_c (FttCell * cell, DropSumsPar * p)
{
  GFS_VALUE (cell, p->c) = gfs_function_value (p->fc, cell);
}

static gboolean gfs_output_droplet_sums_event (GfsEvent * event, GfsSimulation * sim)
{
  if ((* GFS_EVENT_CLASS (GTS_OBJECT_CLASS (gfs_output_droplet_sums_class ())->parent_class)->event) (event, sim)) {
    GfsOutputDropletSums * d = GFS_OUTPUT_DROPLET_SUMS (event);
    GfsDomain * domain = GFS_DOMAIN (sim);
    DropSumsPar p;
    p.s = GFS_OUTPUT_SCALAR (event)->v;
    p.c = gfs_function_get_variable (d->c);
    if (!p.c) {
      p.c = gfs_temporary_variable (domain);
      p.fc = d->c;
      gfs_domain_cell_traverse (domain, FTT_PRE_ORDER, FTT_TRAVERSE_ALL, -1,
				(FttCellTraverseFunc) compute_c, &p);
    }
    p.tag = d->tag ? d->tag : gfs_temporary_variable (domain);
    p.n = gfs_domain_tag_droplets (domain, p.c, p.tag);
    if (p.n > 0) {
      p.v = g_malloc0 (p.n*sizeof (double));
      gfs_domain_cell_traverse (domain, FTT_PRE_ORDER, FTT_TRAVERSE_LEAFS, -1,
				(FttCellTraverseFunc) droplet_sums, &p);
      guint i;
      for (i = 0; i < p.n; i++)
	fprintf (GFS_OUTPUT (event)->file->fp, "%g %d %.12g\n", sim->time.t, i + 1, p.v[i]);
      g_free (p.v);
    }
    if (p.tag != d->tag)
      gts_object_destroy (GTS_OBJECT (p.tag));
    if (!gfs_function_get_variable (d->c))
      gts_object_destroy (GTS_OBJECT (p.c));
    return TRUE;
  }
  return FALSE;
}

static void gfs_output_droplet_sums_class_init (GfsOutputClass * klass)
{
  GFS_EVENT_CLASS (klass)->event = gfs_output_droplet_sums_event;
  GTS_OBJECT_CLASS (klass)->read = gfs_output_droplet_sums_read;
  GTS_OBJECT_CLASS (klass)->write = gfs_output_droplet_sums_write;
  GTS_OBJECT_CLASS (klass)->destroy = gfs_output_droplet_sums_destroy;
}

static void gfs_output_droplet_sums_init (GfsOutputDropletSums * d)
{
  d->c = gfs_function_new (gfs_function_class (), 0.);
}

GfsOutputClass * gfs_output_droplet_sums_class (void)
{
  static GfsOutputClass * klass = NULL;

  if (klass == NULL) {
    GtsObjectClassInfo gfs_output_droplet_sums_info = {
      "GfsOutputDropletSums",
      sizeof (GfsOutputDropletSums),
      sizeof (GfsOutputClass),
      (GtsObjectClassInitFunc) gfs_output_droplet_sums_class_init,
      (GtsObjectInitFunc) gfs_output_droplet_sums_init,
      (GtsArgSetFunc) NULL,
      (GtsArgGetFunc) NULL
    };
    klass = gts_object_class_new (GTS_OBJECT_CLASS (gfs_output_scalar_class ()),
				  &gfs_output_droplet_sums_info);
  }

  return klass;
}

/* GfsOutputErrorNorm: Object */

static void output_error_norm_destroy (GtsObject * o)
{
  gts_object_destroy (GTS_OBJECT (GFS_OUTPUT_ERROR_NORM (o)->s));
  gts_object_destroy (GTS_OBJECT (GFS_OUTPUT_ERROR_NORM (o)->w));

  (* GTS_OBJECT_CLASS (gfs_output_error_norm_class ())->parent_class->destroy) (o);
}

static void output_error_norm_read (GtsObject ** o, GtsFile * fp)
{
  GfsOutputErrorNorm * n;

  if (GTS_OBJECT_CLASS (gfs_output_error_norm_class ())->parent_class->read)
    (* GTS_OBJECT_CLASS (gfs_output_error_norm_class ())->parent_class->read) (o, fp);
  if (fp->type == GTS_ERROR)
    return;

  n = GFS_OUTPUT_ERROR_NORM (*o);
  if (fp->type != '{') {
    gts_file_error (fp, "expecting an opening brace");
    return;
  }
  fp->scope_max++;
  gts_file_next_token (fp);
  while (fp->type != GTS_ERROR && fp->type != '}') {
    if (fp->type == '\n') {
      gts_file_next_token (fp);
      continue;
    }
    if (fp->type != GTS_STRING) {
      gts_file_error (fp, "expecting a parameter");
      return;
    }
    else if (!strcmp (fp->token->str, "unbiased")) {
      gts_file_next_token (fp);
      if (fp->type != '=') {
	gts_file_error (fp, "expecting `='");
	return;
      }
      gts_file_next_token (fp);
      if (fp->type != GTS_INT) {
	gts_file_error (fp, "expecting an integer");
	return;
      }
      n->unbiased = atoi (fp->token->str);
      gts_file_next_token (fp);
    }
    else if (!strcmp (fp->token->str, "s")) {
      gts_file_next_token (fp);
      if (fp->type != '=') {
	gts_file_error (fp, "expecting `='");
	return;
      }
      gts_file_next_token (fp);
      gfs_function_read (n->s, gfs_object_simulation (*o), fp);
      if (fp->type == GTS_ERROR)
	return;
    }
    else if (!strcmp (fp->token->str, "w")) {
      gts_file_next_token (fp);
      if (fp->type != '=') {
	gts_file_error (fp, "expecting `='");
	return;
      }
      gts_file_next_token (fp);
      gfs_function_read (n->w, gfs_object_simulation (*o), fp);
      if (fp->type == GTS_ERROR)
	return;
    }
    else if (!strcmp (fp->token->str, "v")) {
      GfsDomain * domain = GFS_DOMAIN (gfs_object_simulation (*o));

      gts_file_next_token (fp);
      if (fp->type != '=') {
	gts_file_error (fp, "expecting `='");
	return;
      }
      gts_file_next_token (fp);
      if (fp->type != GTS_STRING) {
	gts_file_error (fp, "expecting a variable name");
	return;
      }
      if (!(n->v = gfs_domain_get_or_add_variable (domain, fp->token->str, "Error field"))) {
	gts_file_error (fp, "`%s' is a reserved keyword", fp->token->str);
	return;
      }
      gts_file_next_token (fp);
    }
    else {
      gts_file_error (fp, "unknown identifier `%s'", fp->token->str);
      return;
    }
  }
  if (fp->type != '}') {
    gts_file_error (fp, "expecting a closing brace");
    return;
  }
  fp->scope_max--;
  gts_file_next_token (fp);
}

static void output_error_norm_write (GtsObject * o, FILE * fp)
{
  GfsOutputErrorNorm * n = GFS_OUTPUT_ERROR_NORM (o);

  if (GTS_OBJECT_CLASS (gfs_output_error_norm_class ())->parent_class->write)
    (* GTS_OBJECT_CLASS (gfs_output_error_norm_class ())->parent_class->write) 
      (o, fp);
  fputs (" { s = ", fp);
  gfs_function_write (n->s, fp);
  fputs (" w = ", fp);
  gfs_function_write (n->w, fp);
  fprintf (fp, " unbiased = %d", n->unbiased);
  if (n->v)
    fprintf (fp, " v = %s }", n->v->name);
  else
    fputs (" }", fp);
}

static void compute_error (FttCell * cell, GfsOutputScalar * o)
{
  GFS_VARIABLE (cell, GFS_OUTPUT_ERROR_NORM (o)->v->i) = GFS_VARIABLE (cell, o->v->i) -
    gfs_function_value (GFS_OUTPUT_ERROR_NORM (o)->s, cell);
}

static void remove_bias (FttCell * cell, gpointer * data)
{
  GfsVariable * v = data[0];
  GfsNorm * norm = data[1];
  GFS_VARIABLE (cell, v->i) -= norm->bias;
}

static gboolean gfs_output_error_norm_event (GfsEvent * event, 
					     GfsSimulation * sim)
{
  if ((* GFS_EVENT_CLASS (GTS_OBJECT_CLASS (gfs_output_error_norm_class ())->parent_class)->event)
      (event, sim)) {
    GfsOutputScalar * output = GFS_OUTPUT_SCALAR (event);
    GfsOutputErrorNorm * enorm = GFS_OUTPUT_ERROR_NORM (event);
    GfsVariable * v = enorm->v;
    GfsNorm norm;

    if (v == NULL)
      enorm->v = gfs_temporary_variable (GFS_DOMAIN (sim));
    gfs_domain_cell_traverse (GFS_DOMAIN (sim), FTT_PRE_ORDER, 
			      FTT_TRAVERSE_LEAFS|FTT_TRAVERSE_LEVEL,  
			      output->maxlevel,
			      (FttCellTraverseFunc) compute_error, output);
    norm = gfs_domain_norm_variable (GFS_DOMAIN (sim), enorm->v, enorm->w,
				     FTT_TRAVERSE_LEAFS|FTT_TRAVERSE_LEVEL, 
				     output->maxlevel);
    if (GFS_OUTPUT_ERROR_NORM (event)->unbiased) {
      gpointer data[2];

      data[0] = enorm->v;
      data[1] = &norm;
      gfs_domain_cell_traverse (GFS_DOMAIN (sim), FTT_PRE_ORDER, 
				FTT_TRAVERSE_LEAFS|FTT_TRAVERSE_LEVEL,  
				output->maxlevel,
				(FttCellTraverseFunc) remove_bias, data);
      norm = gfs_domain_norm_variable (GFS_DOMAIN (sim), enorm->v, enorm->w,
				       FTT_TRAVERSE_LEAFS|FTT_TRAVERSE_LEVEL, 
				       output->maxlevel);
    }
    if (v == NULL) {
      gts_object_destroy (GTS_OBJECT (enorm->v));
      enorm->v = NULL;
    }
    fprintf (GFS_OUTPUT (event)->file->fp,
	     "%s time: %g first: % 10.3e second: % 10.3e infty: % 10.3e bias: %10.3e\n",
	     output->name, sim->time.t,
	     norm.first, norm.second, norm.infty, norm.bias);
    return TRUE;
  }
  return FALSE;
}

static void gfs_output_error_norm_class_init (GfsOutputClass * klass)
{
  GTS_OBJECT_CLASS (klass)->destroy = output_error_norm_destroy;
  GTS_OBJECT_CLASS (klass)->read = output_error_norm_read;
  GTS_OBJECT_CLASS (klass)->write = output_error_norm_write;
  GFS_EVENT_CLASS (klass)->event = gfs_output_error_norm_event;
}

static void output_error_norm_init (GfsOutputErrorNorm * e)
{
  e->s = gfs_function_new (gfs_function_class (), 0.);
  e->w = gfs_function_new (gfs_function_class (), 1.);
}

GfsOutputClass * gfs_output_error_norm_class (void)
{
  static GfsOutputClass * klass = NULL;

  if (klass == NULL) {
    GtsObjectClassInfo gfs_output_error_norm_info = {
      "GfsOutputErrorNorm",
      sizeof (GfsOutputErrorNorm),
      sizeof (GfsOutputClass),
      (GtsObjectClassInitFunc) gfs_output_error_norm_class_init,
      (GtsObjectInitFunc) output_error_norm_init,
      (GtsArgSetFunc) NULL,
      (GtsArgGetFunc) NULL
    };
    klass = gts_object_class_new (GTS_OBJECT_CLASS (gfs_output_scalar_class ()),
				  &gfs_output_error_norm_info);
  }

  return klass;
}

/* GfsOutputCorrelation: Object */

static void compute_correlation (FttCell * cell, gpointer * data)
{
  GfsOutputScalar * o = data[0];
  gdouble * bias = data[1];
  gdouble * sum = data[2];
  gdouble * sumref = data[3];
  gdouble v, ref, w;

  ref = gfs_function_value (GFS_OUTPUT_ERROR_NORM (o)->s, cell);
  v = GFS_VARIABLE (cell, o->v->i) - *bias;
  w = gfs_cell_volume (cell);
  *sumref += ref*ref*w;
  *sum += v*ref*w;
}

static gboolean gfs_output_correlation_event (GfsEvent * event, 
					      GfsSimulation * sim)
{
  if ((* GFS_EVENT_CLASS (GTS_OBJECT_CLASS (gfs_output_error_norm_class ())->parent_class)->event)
      (event, sim)) {
    GfsOutputScalar * output = GFS_OUTPUT_SCALAR (event);
    GfsOutputErrorNorm * enorm = GFS_OUTPUT_ERROR_NORM (event);
    GfsVariable * v = enorm->v;
    gdouble bias = 0., sum = 0., sumref = 0.;
    gpointer data[4];

    if (GFS_DOMAIN (sim)->pid != -1)
      g_assert_not_implemented ();

    if (v == NULL)
      enorm->v = gfs_temporary_variable (GFS_DOMAIN (sim));
    if (enorm->unbiased) {
      gfs_domain_cell_traverse (GFS_DOMAIN (sim), FTT_PRE_ORDER,
				FTT_TRAVERSE_LEAFS|FTT_TRAVERSE_LEVEL,
				output->maxlevel,
				(FttCellTraverseFunc) compute_error, output);
      bias = gfs_domain_norm_variable (GFS_DOMAIN (sim), enorm->v, NULL,
				       FTT_TRAVERSE_LEAFS|FTT_TRAVERSE_LEVEL, 
				       output->maxlevel).bias;
    }
    data[0] = output;
    data[1] = &bias;
    data[2] = &sum;
    data[3] = &sumref;
    gfs_domain_cell_traverse (GFS_DOMAIN (sim), FTT_PRE_ORDER,
			      FTT_TRAVERSE_LEAFS|FTT_TRAVERSE_LEVEL,
			      output->maxlevel,
			      (FttCellTraverseFunc) compute_correlation, data);
    if (v == NULL) {
      gts_object_destroy (GTS_OBJECT (enorm->v));
      enorm->v = NULL;
    }
    fprintf (GFS_OUTPUT (event)->file->fp,
	     "%s time: %g %10.3e\n",
	     output->name, sim->time.t, sumref > 0. ? sum/sumref : 0.);
    return TRUE;
  }
  return FALSE;
}

static void gfs_output_correlation_class_init (GfsOutputClass * klass)
{
  GFS_EVENT_CLASS (klass)->event = gfs_output_correlation_event;
}

GfsOutputClass * gfs_output_correlation_class (void)
{
  static GfsOutputClass * klass = NULL;

  if (klass == NULL) {
    GtsObjectClassInfo gfs_output_correlation_info = {
      "GfsOutputCorrelation",
      sizeof (GfsOutputErrorNorm),
      sizeof (GfsOutputClass),
      (GtsObjectClassInitFunc) gfs_output_correlation_class_init,
      (GtsObjectInitFunc) NULL,
      (GtsArgSetFunc) NULL,
      (GtsArgGetFunc) NULL
    };
    klass = gts_object_class_new (GTS_OBJECT_CLASS (gfs_output_error_norm_class ()),
				  &gfs_output_correlation_info);
  }

  return klass;
}

/* GfsOutputSquares: Object */

static gboolean gfs_output_squares_event (GfsEvent * event, GfsSimulation * sim)
{
  if ((* GFS_EVENT_CLASS (GTS_OBJECT_CLASS (gfs_output_squares_class ())->parent_class)->event)
      (event, sim)) {
    GfsOutputScalar * output = GFS_OUTPUT_SCALAR (event);
    
    gfs_write_squares (GFS_DOMAIN (sim), 
		       output->v, output->min, output->max,
		       FTT_TRAVERSE_LEAFS|FTT_TRAVERSE_LEVEL,
		       output->maxlevel, NULL, 
		       GFS_OUTPUT (event)->file->fp);
    fflush (GFS_OUTPUT (event)->file->fp);
    return TRUE;
  }
  return FALSE;
}

static void gfs_output_squares_class_init (GfsOutputClass * klass)
{
  GFS_EVENT_CLASS (klass)->event = gfs_output_squares_event;
}

GfsOutputClass * gfs_output_squares_class (void)
{
  static GfsOutputClass * klass = NULL;

  if (klass == NULL) {
    GtsObjectClassInfo gfs_output_squares_info = {
      "GfsOutputSquares",
      sizeof (GfsOutputScalar),
      sizeof (GfsOutputClass),
      (GtsObjectClassInitFunc) gfs_output_squares_class_init,
      (GtsObjectInitFunc) NULL,
      (GtsArgSetFunc) NULL,
      (GtsArgGetFunc) NULL
    };
    klass = gts_object_class_new (GTS_OBJECT_CLASS (gfs_output_scalar_class ()),
				  &gfs_output_squares_info);
  }

  return klass;
}

/* GfsOutputStreamline: Object */

static void gfs_output_streamline_read (GtsObject ** o, GtsFile * fp)
{
  GfsOutputStreamline * l = GFS_OUTPUT_STREAMLINE (*o);

  if (GTS_OBJECT_CLASS (gfs_output_streamline_class ())->parent_class->read)
    (* GTS_OBJECT_CLASS (gfs_output_streamline_class ())->parent_class->read) 
      (o, fp);
  if (fp->type == GTS_ERROR)
    return;

  if (fp->type != GTS_INT && fp->type != GTS_FLOAT) {
    gts_file_error (fp, "expecting a number (p.x)");
    return;
  }
  l->p.x = atof (fp->token->str);
  gts_file_next_token (fp);

  if (fp->type != GTS_INT && fp->type != GTS_FLOAT) {
    gts_file_error (fp, "expecting a number (p.y)");
    return;
  }
  l->p.y = atof (fp->token->str);
  gts_file_next_token (fp);

  if (fp->type != GTS_INT && fp->type != GTS_FLOAT) {
    gts_file_error (fp, "expecting a number (p.z)");
    return;
  }
  l->p.z = atof (fp->token->str);
  gts_file_next_token (fp);
}

static void gfs_output_streamline_write (GtsObject * o, FILE * fp)
{
  GfsOutputStreamline * l = GFS_OUTPUT_STREAMLINE (o);

  if (GTS_OBJECT_CLASS (gfs_output_streamline_class ())->parent_class->write)
    (* GTS_OBJECT_CLASS (gfs_output_streamline_class ())->parent_class->write) 
      (o, fp);
  fprintf (fp, " %g %g %g", l->p.x, l->p.y, l->p.z);
}

static gboolean gfs_output_streamline_event (GfsEvent * event, 
					    GfsSimulation * sim)
{
  if ((* GFS_EVENT_CLASS (GTS_OBJECT_CLASS (gfs_output_streamline_class ())->parent_class)->event)
      (event,sim)) {
    GList * stream = gfs_streamline_new (GFS_DOMAIN (sim),
					 gfs_domain_velocity (GFS_DOMAIN (sim)),
					 GFS_OUTPUT_STREAMLINE (event)->p,
					 GFS_OUTPUT_SCALAR (event)->v,
					 0., 0.,
					 TRUE,
					 NULL, NULL);

    gfs_streamline_write (stream, GFS_OUTPUT (event)->file->fp);
    fflush (GFS_OUTPUT (event)->file->fp);
    gfs_streamline_destroy (stream);
    return TRUE;
  }
  return FALSE;
}

static void gfs_output_streamline_class_init (GfsOutputClass * klass)
{
  GFS_EVENT_CLASS (klass)->event = gfs_output_streamline_event;
  GTS_OBJECT_CLASS (klass)->read = gfs_output_streamline_read;
  GTS_OBJECT_CLASS (klass)->write = gfs_output_streamline_write;
}

GfsOutputClass * gfs_output_streamline_class (void)
{
  static GfsOutputClass * klass = NULL;

  if (klass == NULL) {
    GtsObjectClassInfo gfs_output_streamline_info = {
      "GfsOutputStreamline",
      sizeof (GfsOutputStreamline),
      sizeof (GfsOutputClass),
      (GtsObjectClassInitFunc) gfs_output_streamline_class_init,
      (GtsObjectInitFunc) NULL,
      (GtsArgSetFunc) NULL,
      (GtsArgGetFunc) NULL
    };
    klass = gts_object_class_new (GTS_OBJECT_CLASS (gfs_output_scalar_class ()),
				  &gfs_output_streamline_info);
  }

  return klass;
}

/* GfsOutputParticle: Object */

static void gfs_output_particle_destroy (GtsObject * o)
{
  GfsOutputParticle * l = GFS_OUTPUT_PARTICLE (o);

  gts_object_destroy (GTS_OBJECT (l->p));
  
  (* GTS_OBJECT_CLASS (gfs_output_particle_class ())->parent_class->destroy) (o);
}

static void gfs_output_particle_read (GtsObject ** o, GtsFile * fp)
{
  GfsOutputParticle * l = GFS_OUTPUT_PARTICLE (*o);

  if (GTS_OBJECT_CLASS (gfs_output_particle_class ())->parent_class->read)
    (* GTS_OBJECT_CLASS (gfs_output_particle_class ())->parent_class->read) 
      (o, fp);
  if (fp->type == GTS_ERROR)
    return;

  if (fp->type != GTS_INT && fp->type != GTS_FLOAT) {
    gts_file_error (fp, "expecting a number (p.x)");
    return;
  }
  l->p->x = atof (fp->token->str);
  gts_file_next_token (fp);

  if (fp->type != GTS_INT && fp->type != GTS_FLOAT) {
    gts_file_error (fp, "expecting a number (p.y)");
    return;
  }
  l->p->y = atof (fp->token->str);
  gts_file_next_token (fp);

  if (fp->type != GTS_INT && fp->type != GTS_FLOAT) {
    gts_file_error (fp, "expecting a number (p.z)");
    return;
  }
  l->p->z = atof (fp->token->str);
  gts_file_next_token (fp);
}

static void gfs_output_particle_write (GtsObject * o, FILE * fp)
{
  GfsOutputParticle * l = GFS_OUTPUT_PARTICLE (o);

  if (GTS_OBJECT_CLASS (gfs_output_particle_class ())->parent_class->write)
    (* GTS_OBJECT_CLASS (gfs_output_particle_class ())->parent_class->write) (o, fp);
  fprintf (fp, " %g %g %g", l->p->x, l->p->y, l->p->z);
}

static gboolean gfs_output_particle_event (GfsEvent * event, 
					   GfsSimulation * sim)
{
  GfsOutputParticle * l = GFS_OUTPUT_PARTICLE (event);
  gboolean ret = FALSE;

  if ((* GFS_EVENT_CLASS (GTS_OBJECT_CLASS (gfs_output_particle_class ())->parent_class)->event)
      (event,sim)) {
    FILE * fp = GFS_OUTPUT (event)->file->fp;

    fprintf (fp, "%g %g %g %g\n", sim->time.t, l->p->x, l->p->y, l->p->z);
    ret = TRUE;
  }
  gfs_domain_advect_point (GFS_DOMAIN (sim), l->p, sim->advection_params.dt);
  return ret;
}

static void gfs_output_particle_class_init (GfsOutputClass * klass)
{
  GFS_EVENT_CLASS (klass)->event = gfs_output_particle_event;
  GTS_OBJECT_CLASS (klass)->read = gfs_output_particle_read;
  GTS_OBJECT_CLASS (klass)->write = gfs_output_particle_write;
  GTS_OBJECT_CLASS (klass)->destroy = gfs_output_particle_destroy;
}

static void gfs_output_particle_init (GfsOutputParticle * l)
{
  l->p = gts_point_new (gts_point_class (), 0., 0., 0.);
}

GfsOutputClass * gfs_output_particle_class (void)
{
  static GfsOutputClass * klass = NULL;

  if (klass == NULL) {
    GtsObjectClassInfo gfs_output_particle_info = {
      "GfsOutputParticle",
      sizeof (GfsOutputParticle),
      sizeof (GfsOutputClass),
      (GtsObjectClassInitFunc) gfs_output_particle_class_init,
      (GtsObjectInitFunc) gfs_output_particle_init,
      (GtsArgSetFunc) NULL,
      (GtsArgGetFunc) NULL
    };
    klass = gts_object_class_new (GTS_OBJECT_CLASS 
				  (gfs_output_class ()),
				  &gfs_output_particle_info);
  }

  return klass;
}

/* GfsOutputPPM: Object */

static void gfs_output_ppm_read (GtsObject ** o, GtsFile * fp)
{
  if (GTS_OBJECT_CLASS (gfs_output_ppm_class ())->parent_class->read)
    (* GTS_OBJECT_CLASS (gfs_output_ppm_class ())->parent_class->read) 
      (o, fp);
  if (fp->type == GTS_ERROR)
    return;
#if (!FTT_2D)
  if (!GFS_IS_OCEAN (gfs_object_simulation (*o))) {
    gts_file_error (fp, 
		    "In more than two dimensions PPM output is possible\n"
		    "only for GfsOcean simulations");
    return;
  }
#endif /* 2D3 or 3D */
}

static gboolean gfs_output_ppm_event (GfsEvent * event, GfsSimulation * sim)
{
  if ((* GFS_EVENT_CLASS (GTS_OBJECT_CLASS (gfs_output_ppm_class ())->parent_class)->event) 
      (event, sim)) {
    GfsOutputScalar * output = GFS_OUTPUT_SCALAR (event);
#if FTT_2D
    GfsDomain * domain = GFS_DOMAIN (sim);
#else /* 2D3 or 3D */
    GfsDomain * domain = GFS_IS_OCEAN (sim) ? GFS_OCEAN (sim)->toplayer : GFS_DOMAIN (sim);
#endif /* 2D3 or 3D */

    gfs_write_ppm (domain,
		   output->box,
		   output->v, output->min, output->max,
		   FTT_TRAVERSE_LEAFS|FTT_TRAVERSE_LEVEL, output->maxlevel,
		   GFS_OUTPUT (event)->file->fp);
    fflush (GFS_OUTPUT (event)->file->fp);
    return TRUE;
  }
  return FALSE;
}

static void gfs_output_ppm_class_init (GfsOutputClass * klass)
{
  GTS_OBJECT_CLASS (klass)->read = gfs_output_ppm_read;
  GFS_EVENT_CLASS (klass)->event = gfs_output_ppm_event;
}

GfsOutputClass * gfs_output_ppm_class (void)
{
  static GfsOutputClass * klass = NULL;

  if (klass == NULL) {
    GtsObjectClassInfo gfs_output_ppm_info = {
      "GfsOutputPPM",
      sizeof (GfsOutputScalar),
      sizeof (GfsOutputClass),
      (GtsObjectClassInitFunc) gfs_output_ppm_class_init,
      (GtsObjectInitFunc) NULL,
      (GtsArgSetFunc) NULL,
      (GtsArgGetFunc) NULL
    };
    klass = gts_object_class_new (GTS_OBJECT_CLASS (gfs_output_scalar_class ()),
				  &gfs_output_ppm_info);
  }

  return klass;
}
