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

#include <math.h>
#include <string.h>
#include "solid.h"
#include "vof.h"

/* GfsFace: Header */

typedef struct _GfsFace         GfsFace;
typedef struct _GfsFaceClass    GfsFaceClass;

struct _GfsFace {
  GtsFace parent;

  FttDirection dir;
};

struct _GfsFaceClass {
  GtsFaceClass parent_class;
};

#define GFS_FACE(obj)            GTS_OBJECT_CAST (obj,\
					           GfsFace,\
					           gfs_face_class ())
#define GFS_FACE_CLASS(klass)    GTS_OBJECT_CLASS_CAST (klass,\
						         GfsFaceClass,\
						         gfs_face_class())
#define GFS_IS_FACE(obj)         (gts_object_is_from_class (obj,\
						   gfs_face_class ()))
     
static GfsFaceClass * gfs_face_class              (void);
static GfsFace *      gfs_face_new                (GfsFaceClass * klass,
						   GtsEdge * e1,
						   GtsEdge * e2,
						   GtsEdge * e3,
						   guint dir);

/* GfsFace: Object */

static void gfs_face_link (GtsObject * object, GtsObject * with)
{
  GFS_FACE (object)->dir = GFS_FACE (with)->dir;
}

static void gfs_face_class_init (GfsFaceClass * klass)
{
  GTS_OBJECT_CLASS (klass)->attributes = gfs_face_link;
}

static void gfs_face_init (GfsFace * f)
{
  f->dir = 0;
}

static GfsFaceClass * gfs_face_class (void)
{
  static GfsFaceClass * klass = NULL;

  if (klass == NULL) {
    GtsObjectClassInfo gfs_face_info = {
      "GfsFace",
      sizeof (GfsFace),
      sizeof (GfsFaceClass),
      (GtsObjectClassInitFunc) gfs_face_class_init,
      (GtsObjectInitFunc) gfs_face_init,
      (GtsArgSetFunc) NULL,
      (GtsArgGetFunc) NULL
    };
    klass = gts_object_class_new (GTS_OBJECT_CLASS (gts_face_class ()),
				  &gfs_face_info);
  }

  return klass;
}

static GfsFace * gfs_face_new (GfsFaceClass * klass,
			     GtsEdge * e1,
			     GtsEdge * e2,
			     GtsEdge * e3,
			     guint dir)
{
  GfsFace * f = GFS_FACE (gts_face_new (GTS_FACE_CLASS (klass), e1, e2, e3));

  f->dir = dir;
  return f;
}

static void surface_add_box (GtsSurface * s,
			     gdouble x1, gdouble y1, gdouble z1,
			     gdouble x2, gdouble y2, gdouble z2)
{
  GtsVertex * v0 = gts_vertex_new (s->vertex_class, x1, y1, z1);
  GtsVertex * v1 = gts_vertex_new (s->vertex_class, x1, y1, z2);
  GtsVertex * v2 = gts_vertex_new (s->vertex_class, x1, y2, z2);
  GtsVertex * v3 = gts_vertex_new (s->vertex_class, x1, y2, z1);
  GtsVertex * v4 = gts_vertex_new (s->vertex_class, x2, y1, z1);
  GtsVertex * v5 = gts_vertex_new (s->vertex_class, x2, y1, z2);
  GtsVertex * v6 = gts_vertex_new (s->vertex_class, x2, y2, z2);
  GtsVertex * v7 = gts_vertex_new (s->vertex_class, x2, y2, z1);

  GtsEdge * e1 = gts_edge_new (s->edge_class, v0, v1);
  GtsEdge * e2 = gts_edge_new (s->edge_class, v1, v2);
  GtsEdge * e3 = gts_edge_new (s->edge_class, v2, v3);
  GtsEdge * e4 = gts_edge_new (s->edge_class, v3, v0);
  GtsEdge * e5 = gts_edge_new (s->edge_class, v0, v2);

  GtsEdge * e6 = gts_edge_new (s->edge_class, v4, v5);
  GtsEdge * e7 = gts_edge_new (s->edge_class, v5, v6);
  GtsEdge * e8 = gts_edge_new (s->edge_class, v6, v7);
  GtsEdge * e9 = gts_edge_new (s->edge_class, v7, v4);
  GtsEdge * e10 = gts_edge_new (s->edge_class, v4, v6);
  
  GtsEdge * e11 = gts_edge_new (s->edge_class, v3, v7);
  GtsEdge * e12 = gts_edge_new (s->edge_class, v2, v6);
  GtsEdge * e13 = gts_edge_new (s->edge_class, v1, v5);
  GtsEdge * e14 = gts_edge_new (s->edge_class, v0, v4);

  GtsEdge * e15 = gts_edge_new (s->edge_class, v1, v6);
  GtsEdge * e16 = gts_edge_new (s->edge_class, v2, v7);
  GtsEdge * e17 = gts_edge_new (s->edge_class, v3, v4);
  GtsEdge * e18 = gts_edge_new (s->edge_class, v0, v5);

  GfsFaceClass * klass = gfs_face_class ();

  gts_surface_add_face (s, GTS_FACE (gfs_face_new (klass, e1, e2, e5, 1)));
  gts_surface_add_face (s, GTS_FACE (gfs_face_new (klass, e5, e3, e4, 1)));
  gts_surface_add_face (s, GTS_FACE (gfs_face_new (klass, e6, e10, e7, 0)));
  gts_surface_add_face (s, GTS_FACE (gfs_face_new (klass, e10, e9, e8, 0)));
  gts_surface_add_face (s, GTS_FACE (gfs_face_new (klass, e2, e15, e12, 4)));
  gts_surface_add_face (s, GTS_FACE (gfs_face_new (klass, e15, e13, e7, 4)));
  gts_surface_add_face (s, GTS_FACE (gfs_face_new (klass, e3, e16, e11, 2)));
  gts_surface_add_face (s, GTS_FACE (gfs_face_new (klass, e16, e12, e8, 2)));
  gts_surface_add_face (s, GTS_FACE (gfs_face_new (klass, e17, e14, e4, 5)));
  gts_surface_add_face (s, GTS_FACE (gfs_face_new (klass, e17, e11, e9, 5)));
  gts_surface_add_face (s, GTS_FACE (gfs_face_new (klass, e18, e13, e1, 3)));
  gts_surface_add_face (s, GTS_FACE (gfs_face_new (klass, e18, e14, e6, 3)));
}

static void cell_size (FttCell * cell, GtsVector s)
{
#if FTT_2D3
  s[0] = s[1] = ftt_cell_size (cell);
  s[2] = 1.;
#else  /* 2D or 3D */
  s[0] = s[1] = s[2] = ftt_cell_size (cell);
#endif /* 2D or 3D */
}

static GtsBBox * bbox_cell (GtsBBoxClass * klass, FttCell * cell)
{
  FttVector p;
  GtsVector size;

  ftt_cell_pos (cell, &p);
  cell_size (cell, size);
  return gts_bbox_new (klass, cell, 
		       p.x - size[0]/2., p.y - size[1]/2., p.z - size[2]/2.,
		       p.x + size[0]/2., p.y + size[1]/2., p.z + size[2]/2.);
}

/**
 * gfs_cell_fluid:
 * @cell: a #FttCell.
 * 
 * Sets @cell and all its descendants as full fluid cells.
 */
void gfs_cell_fluid (FttCell * cell)
{
  g_return_if_fail (cell != NULL);

  if (GFS_STATE (cell)->solid) {
    g_free (GFS_STATE (cell)->solid);
    GFS_STATE (cell)->solid = NULL;
  }

  if (!FTT_CELL_IS_LEAF (cell)) {
    FttCellChildren child;
    guint i;
 
    ftt_cell_children (cell, &child);
    for (i = 0; i < FTT_CELLS; i++)
      if (child.c[i])
	gfs_cell_fluid (child.c[i]);
  }
}

static void gfs_cell_solid (FttCell * cell)
{
  g_return_if_fail (cell != NULL);

  if (GFS_STATE (cell)->solid == NULL)
    GFS_STATE (cell)->solid = g_malloc0 (sizeof (GfsSolidVector));
  else
    memset (GFS_STATE (cell)->solid, 0, sizeof (GfsSolidVector));

  if (!FTT_CELL_IS_LEAF (cell)) {
    FttCellChildren child;
    guint i;
 
    ftt_cell_children (cell, &child);
    for (i = 0; i < FTT_CELLS; i++)
      if (child.c[i])
	gfs_cell_solid (child.c[i]);
  }
}

static void add_surface_fraction (GfsFace * f, GfsSolidVector * solid)
{
  if (f->dir < FTT_NEIGHBORS)
    solid->s[f->dir] += gts_triangle_area (GTS_TRIANGLE (f));
}

static guint warning_number = 0;

static void write_surface_warning (GtsSurfaceInter * si,
				   GtsSurface * cs,
				   GtsSurface * inter)
{
  gchar s[80];
  FILE * fp;
  
  sprintf (s, "/tmp/gerris_warning.%d", warning_number++);
  fp = fopen (s, "wt");
  if (fp != NULL) {
#if 1
    gts_surface_write (cs, fp);
#else
    GSList * i = si->edges;
    
    fputs ("(geometry \"cell\" = {\n", fp);
    gts_surface_write_oogl (cs, fp);
    fputs ("})\n(geometry \"inter\" = {\n", fp);
    gts_surface_write_oogl (inter, fp);
    fputs ("})\n(geometry \"curve\" = LIST {\n", fp);
    while (i) {
      GtsPoint * p1 = GTS_POINT (GTS_SEGMENT (i->data)->v1);
      GtsPoint * p2 = GTS_POINT (GTS_SEGMENT (i->data)->v2);
      
      fprintf (fp, "VECT 1 2 0 2 0 %g %g %g %g %g %g\n",
	       p1->x, p1->y, p1->z, p2->x, p2->y, p2->z);
      i = i->next;
    }
    fputs ("})\n", fp);
#endif
    fclose (fp);
  }
}

static void bbox_size (GtsBBox * bbox, GtsVector s)
{
  s[0] = bbox->x2 - bbox->x1;
  s[1] = bbox->y2 - bbox->y1;
  s[2] = bbox->z2 - bbox->z1;
}

static void set_solid_fractions_from_surface (FttCell * cell,
					      GtsBBox * bbox,
					      GtsSurface * s,
					      GNode * stree,
					      gboolean is_open,
					      gboolean destroy_solid,
					      FttCellCleanupFunc cleanup,
					      gpointer data)
{
  GtsSurface * cs; 
  GNode * ctree;
  GtsSurfaceInter * si;
  GtsVector size;
  gboolean closed = TRUE;

  bbox_size (bbox, size);
  cs = gts_surface_new (gts_surface_class (),
			GTS_FACE_CLASS (gfs_face_class ()),
			gts_edge_class (),
			gts_vertex_class ());
  surface_add_box (cs, 
		   bbox->x1, bbox->y1, bbox->z1,
		   bbox->x2, bbox->y2, bbox->z2);
  ctree = gts_bb_tree_surface (cs);
  si = gts_surface_inter_new (gts_surface_inter_class (),
			      cs, s, ctree, stree, FALSE, is_open);
#if 1
  g_assert (gts_surface_inter_check (si, &closed));
#else
  if (!gts_surface_inter_check (si, &closed)) {
    static guint nerror = 0;
    gchar name[80];
    FILE * fp;

    sprintf (name, "/tmp/cs.%d.gts", nerror++);
    fp = fopen (name, "wt");
    gts_surface_write (cs, fp);
    fclose (fp);

    fp = fopen ("/tmp/s.gts", "wt");
    gts_surface_write (s, fp);
    fclose (fp);

    g_return_if_fail (si != NULL);
    g_return_if_fail (!gts_surface_inter_check (si, &closed));
  }
#endif
  if (si->edges != NULL) {
    GtsSurface * inter = gts_surface_new (gts_surface_class (),
					  gts_face_class (),
					  gts_edge_class (),
					  gts_vertex_class ());
    GtsSurface * inter1 = gts_surface_new (gts_surface_class (),
					   gts_face_class (),
					   gts_edge_class (),
					   gts_vertex_class ());
    gdouble size2[3] = { size[1]*size[2], size[0]*size[2], size[0]*size[1] };
    guint i;
    GfsSolidVector * solid = GFS_STATE (cell)->solid;

#if 1
    g_assert (closed);
#else
    if (!closed) {
      GtsGraph * g = gts_segments_graph_new (gts_graph_class (), si->edges);

      gts_graph_write_dot (g, stdout);
      g_return_if_fail (!closed);
      //      g_assert_not_reached ();
    }
#endif
    gts_surface_inter_boolean (si, inter, GTS_1_IN_2);
    if (solid == NULL)
      GFS_STATE (cell)->solid = solid = 
	g_malloc0 (sizeof (GfsSolidVector));
    for (i = 0; i < FTT_NEIGHBORS; i++)
      solid->s[i] = 0.;
    gts_surface_foreach_face (inter, (GtsFunc) add_surface_fraction, solid);
    for (i = 0; i < FTT_NEIGHBORS; i++) {
      solid->s[i] /= size2[i/2];
#if 0
      g_assert (solid->s[i] >= 0. && solid->s[i] <= 1.);
#else
      if (solid->s[i] < 0. || solid->s[i] > 1.) {
	FttVector p;
	
	write_surface_warning (si, cs, inter);
	ftt_cell_pos (cell, &p);
	g_warning ("file %s: line %d (%s): (%g,%g,%g): level %d: solid->s[%d] = %g. "
		   "Surfaces have been written in /tmp/gerris_warning.%d.",
		   __FILE__, __LINE__, G_GNUC_PRETTY_FUNCTION,
		   p.x, p.y, p.z,
		   ftt_cell_level (cell),
		   i, solid->s[i],
		   warning_number - 1);
	if (solid->s[i] > 1.)
	  solid->s[i] = 1.;
	else if (solid->s[i] < 0.)
	  solid->s[i] = 0.;
      }
#endif
    }

    gts_surface_inter_boolean (si, inter1, GTS_2_IN_1);
    gts_surface_merge (inter, inter1);
    solid->a = gts_surface_center_of_mass (inter, &solid->cm.x)/(size[0]*size[1]*size[2]);
    gts_surface_center_of_area (inter1, &solid->ca.x);

#if 0
    g_assert (solid->a > 0. && solid->a < 1.);
#else
    if (solid->a <= 0. || solid->a >= 1.) {
      FttVector p;

      write_surface_warning (si, cs, inter);
      ftt_cell_pos (cell, &p);
      g_warning ("file %s: line %d (%s): (%g,%g,%g): level %d: solid->a = %g. "
		 "Surfaces have been written in /tmp/gerris_warning.%d.",
		 __FILE__, __LINE__, G_GNUC_PRETTY_FUNCTION,
		 p.x, p.y, p.z,
		 ftt_cell_level (cell),
		 solid->a,
		 warning_number - 1);
      if (solid->a > 1.)
	solid->a = 1.;
      else if (solid->a < 0.)
	solid->a = 0.;
    }
#endif

    gts_object_destroy (GTS_OBJECT (inter));
    gts_object_destroy (GTS_OBJECT (inter1));
  }

  gts_object_destroy (GTS_OBJECT (si));
  gts_bb_tree_destroy (ctree, TRUE);
  gts_object_destroy (GTS_OBJECT (cs));
}

static gdouble solid_sa (GfsSolidVector * s)
{
  gdouble sa2 = 0.;
  FttComponent c;

  for (c = 0; c < FTT_DIMENSION; c++) {
    gdouble n = s->s[2*c] - s->s[2*c + 1];

    sa2 += n*n;
  }
  return sqrt (sa2);
}

/**
 * gfs_cell_init_solid_fractions_from_children:
 * @cell: a #FttCell.
 *
 * Uses the values of the solid fractions of the children of @cell to
 * compute the values of its solid fractions.
 *
 * This function fails if @cell is a leaf of the cell tree.  
 */
void gfs_cell_init_solid_fractions_from_children (FttCell * cell)
{
  FttCellChildren child;
  guint i, j;
  gdouble w = 0., wa = 0.;
  gboolean cell_is_solid = TRUE;
  gboolean cell_is_mixed = FALSE;
  FttVector cm = { 0., 0., 0.};
  FttVector ca = { 0., 0., 0.};

  g_return_if_fail (cell != NULL);
  g_return_if_fail (!FTT_CELL_IS_LEAF (cell));

  ftt_cell_children (cell, &child);
  for (i = 0; i < FTT_CELLS; i++)
    if (child.c[i]) {
      if (GFS_IS_FLUID (child.c[i])) {
	FttVector p;

	w += 1.;
	ftt_cell_pos (child.c[i], &p);
	cm.x += p.x; cm.y += p.y; cm.z += p.z;
	cell_is_solid = FALSE;
      }
      else {
	GfsSolidVector * solid = GFS_STATE (child.c[i])->solid;
	gdouble sa = solid_sa (solid);

	w += solid->a; wa += sa;
	cm.x += solid->cm.x*solid->a;
	cm.y += solid->cm.y*solid->a;
	cm.z += solid->cm.z*solid->a;
	ca.x += solid->ca.x*sa;
	ca.y += solid->ca.y*sa;
	ca.z += solid->ca.z*sa;
	cell_is_mixed = TRUE;
      }
    }

  if (cell_is_mixed) {
    GfsSolidVector * solid = GFS_STATE (cell)->solid;

    if (solid == NULL)
      GFS_STATE (cell)->solid = solid = g_malloc0 (sizeof (GfsSolidVector));

    solid->a = w/FTT_CELLS; 
    solid->cm.x = cm.x/w;
    solid->cm.y = cm.y/w;
    solid->cm.z = cm.z/w;
    solid->ca.x = ca.x/wa;
    solid->ca.y = ca.y/wa;
    solid->ca.z = ca.z/wa;
    for (i = 0; i < FTT_NEIGHBORS; i++) {
      guint n = ftt_cell_children_direction (cell, i, &child);

      w = 0.;
      for (j = 0; j < n; j++) {
	FttCell * c = child.c[j];

	if (c) {
	  if (GFS_IS_FLUID (c))
	    w += 1.;
	  else
	    w += GFS_STATE (c)->solid->s[i];
	}
      }
      solid->s[i] = w/n;
    }
  }
  else { /* !cell_is_mixed */
    if (GFS_STATE (cell)->solid) {
      g_free (GFS_STATE (cell)->solid);
      GFS_STATE (cell)->solid = NULL;
    }
    g_assert (!cell_is_solid);
  }
}

static void push_leaf (GtsFifo * fifo, FttCell * cell, FttDirection d, gdouble a)
{
  if (FTT_CELL_IS_LEAF (cell)) {
    if (!GFS_IS_MIXED (cell) && GFS_STATE (cell)->div == 0.) {
      GFS_STATE (cell)->div = a;
      gts_fifo_push (fifo, cell);
    }
  }
  else {
    FttCellChildren child;
    guint i, n;
    
    n = ftt_cell_children_direction (cell, FTT_OPPOSITE_DIRECTION (d), &child);
    for (i = 0; i < n; i++)
      if (child.c[i] && !GFS_IS_MIXED (child.c[i]) && GFS_STATE (child.c[i])->div == 0.) {
	g_assert (FTT_CELL_IS_LEAF (child.c[i]));
	GFS_STATE (child.c[i])->div = a;
	gts_fifo_push (fifo, child.c[i]);
      }
  }  
}

static void paint_leaf (GtsFifo * fifo, gdouble a)
{
  FttCell * cell;

  while ((cell = gts_fifo_pop (fifo))) {
    FttDirection i;
    FttCellNeighbors n;
    
    ftt_cell_neighbors (cell, &n);
    for (i = 0; i < FTT_NEIGHBORS; i++)
      if (n.c[i])
	push_leaf (fifo, n.c[i], i, a);
  }
}

static void paint_mixed_leaf (FttCell * cell)
{
  if (GFS_IS_MIXED (cell)) {
    GfsSolidVector * solid = GFS_STATE (cell)->solid;
    GtsFifo * fifo;
    FttCell * n;
    FttDirection i;
    
    fifo = gts_fifo_new ();
    for (i = 0; i < FTT_NEIGHBORS; i++)
      if ((solid->s[i] == 0. || solid->s[i] == 1.) &&
	  (n = ftt_cell_neighbor (cell, i))) {
	push_leaf (fifo, n, i, solid->s[i] + 1.);
	paint_leaf (fifo, solid->s[i] + 1.);
      }
    gts_fifo_destroy (fifo);
  }
}


typedef struct {
  gboolean is_open, destroy_solid;
  FttCellCleanupFunc cleanup;
  gpointer data;
} InitSolidParams;

static void init_solid_fractions (FttCell * cell, GtsSurface * s, InitSolidParams * p)
{
  GtsBBox * bbox;
  GNode * stree;

  bbox = bbox_cell (gts_bbox_class (), cell);
  stree = gts_bb_tree_surface (s);
  set_solid_fractions_from_surface (cell, bbox, s, stree,
  				    p->is_open, p->destroy_solid, p->cleanup, p->data);
  gts_bb_tree_destroy (stree, TRUE);
  gts_object_destroy (GTS_OBJECT (bbox));
}

static void solid_fractions_from_children (FttCell * cell, InitSolidParams * p)
{
  if (FTT_CELL_IS_LEAF (cell)) {
    if (p->destroy_solid && GFS_STATE (cell)->div == 1.)
      ftt_cell_destroy (cell, p->cleanup, p->data);      
  }
  else {
    FttCellChildren child;
    guint i;
    
    ftt_cell_children (cell, &child);
    for (i = 0; i < FTT_CELLS; i++)
      if (child.c[i])
	solid_fractions_from_children (child.c[i], p);
    if (FTT_CELL_IS_LEAF (cell)) {
      /* all the children have been destroyed i.e. the cell is solid */
      if (FTT_CELL_IS_ROOT (cell))
	g_log (G_LOG_DOMAIN,
	       G_LOG_LEVEL_ERROR,
	       "root cell is entirely outside of the fluid domain\n"
	       "the solid surface orientation may be incorrect\n");
      else
	ftt_cell_destroy (cell, p->cleanup, p->data);
    }
    else
      gfs_cell_init_solid_fractions_from_children (cell);
  }
}

/**
 * gfs_cell_init_solid_fractions:
 * @root: the root #FttCell of the cell tree.
 * @s: a closed, orientable surface defining the solid boundary.
 * @is_open: %TRUE if @s is an "open" boundary i.e. the signed volume
 * enclosed by @s is negative, %FALSE otherwise.
 * @destroy_solid: controls what to do with solid cells.
 * @cleanup: a #FttCellCleanupFunc or %NULL.
 * @data: user data to pass to @cleanup.
 *
 * Initializes the solid fractions of all the cells of the cell tree
 * starting at @root.
 *
 * If @destroy_solid is set to %TRUE, the cells entirely contained in
 * the solid are destroyed using @cleanup as cleanup function.  
 */
void gfs_cell_init_solid_fractions (FttCell * root,
				    GtsSurface * s,
				    gboolean is_open,
				    gboolean destroy_solid,
				    FttCellCleanupFunc cleanup,
				    gpointer data)
{
  InitSolidParams p;

  g_return_if_fail (root != NULL);
  g_return_if_fail (s != NULL);

  p.is_open = is_open;
  p.destroy_solid = destroy_solid;
  p.cleanup = cleanup;
  p.data = data;
  gfs_cell_traverse_cut (root, s, FTT_PRE_ORDER, FTT_TRAVERSE_LEAFS,
			 (FttCellTraverseCutFunc) init_solid_fractions, &p);
  ftt_cell_traverse (root, FTT_PRE_ORDER, FTT_TRAVERSE_LEAFS, -1,
		     (FttCellTraverseFunc) gfs_cell_reset, gfs_div);
  ftt_cell_traverse (root, FTT_PRE_ORDER, FTT_TRAVERSE_LEAFS, -1,
		     (FttCellTraverseFunc) paint_mixed_leaf, NULL);
  solid_fractions_from_children (root, &p);
}

static gboolean check_area_fractions (const FttCell * root)
{
  guint i, level;
  FttCellNeighbors neighbor;
  gboolean ret = TRUE;
  GfsSolidVector * solid;

  level = ftt_cell_level (root);
  ftt_cell_neighbors (root, &neighbor);
  solid = GFS_STATE (root)->solid;

  for (i = 0; i < FTT_NEIGHBORS; i++)
    if (neighbor.c[i]) {
      GfsSolidVector * nsolid = GFS_STATE (neighbor.c[i])->solid;

      if (ftt_cell_level (neighbor.c[i]) == level) {
	if (GFS_IS_FLUID (root)) {
	  if (!GFS_IS_FLUID (neighbor.c[i]) && 
	      1. - nsolid->s[FTT_OPPOSITE_DIRECTION (i)] >= 1e-10) {
	    g_warning ("file %s: line %d (%s): s[%d]: %g",
		       __FILE__, __LINE__, G_GNUC_PRETTY_FUNCTION,
		       FTT_OPPOSITE_DIRECTION (i),
		       nsolid->s[FTT_OPPOSITE_DIRECTION (i)]);
	    ret = FALSE;
	    nsolid->s[FTT_OPPOSITE_DIRECTION (i)] = 1.;
	  }
	}
	else if (GFS_IS_MIXED (neighbor.c[i])) {
	  if (fabs (solid->s[i] - 
		    nsolid->s[FTT_OPPOSITE_DIRECTION (i)]) >= 1e-10) {
	    g_warning ("file %s: line %d (%s): s[%d]: %g neighbor->s[%d]: %g",
		       __FILE__, __LINE__, G_GNUC_PRETTY_FUNCTION,
		       i, solid->s[i],		       
		       FTT_OPPOSITE_DIRECTION (i),
		       nsolid->s[FTT_OPPOSITE_DIRECTION (i)]);
	    ret = FALSE;
	    nsolid->s[FTT_OPPOSITE_DIRECTION (i)] = solid->s[i];
	  }
	}
	else if (1. - solid->s[i] >= 1e-10) {
	  g_warning ("file %s: line %d (%s): s[%d]: %g",
		     __FILE__, __LINE__, G_GNUC_PRETTY_FUNCTION,
		     i, solid->s[i]);
	  ret = FALSE;
	  solid->s[i] = 1.;
	}
      }
      else { /* fine/coarse boundary */
	g_assert (ftt_cell_level (neighbor.c[i]) == level - 1);
	/* check for mixed cell refinement violation (topology.fig) */
	g_return_val_if_fail (GFS_IS_FLUID (neighbor.c[i]), FALSE);
	if (GFS_IS_MIXED (root) && 1. - solid->s[i] >= 1e-10) {
	  g_warning ("file %s: line %d (%s): s[%d]: %g",
		     __FILE__, __LINE__, G_GNUC_PRETTY_FUNCTION,
		     i, solid->s[i]);
	  ret = FALSE;
	  solid->s[i] = 1.;
	}
      }
    }
  
  if (!FTT_CELL_IS_LEAF (root)) {
    FttCellChildren child;

    ftt_cell_children (root, &child);
    for (i = 0; i < FTT_CELLS; i++)
      if (child.c[i] && !check_area_fractions (child.c[i]))
	ret = FALSE;
  }

  return ret;
}

static void check_solid_fractions (FttCell * cell, gboolean * ret)
{
  FttCellChildren children;
  guint n;

  ftt_cell_children (cell, &children);
  if (!GFS_IS_MIXED (cell)) {
    for (n = 0; n < FTT_CELLS; n++)
      if (children.c[n] && GFS_IS_MIXED (children.c[n])) {
	g_warning ("file %s: line %d (%s): children[%d] is mixed (%g)"
		   " parent is not",
                   __FILE__, __LINE__, G_GNUC_PRETTY_FUNCTION,
		   n, GFS_STATE (children.c[n])->solid->a);
	*ret = FALSE;
      }
  }
  else {
    gdouble a = 0.;

    for (n = 0; n < FTT_CELLS; n++)
      if (children.c[n]) {
	if (GFS_IS_MIXED (children.c[n]))
	  a += GFS_STATE (children.c[n])->solid->a;
	else
	  a += 1.;
      }
    a /= FTT_CELLS;
    if (fabs (GFS_STATE (cell)->solid->a - a) >= 1e-10) {
      g_warning ("file %s: line %d (%s): children->a: %g parent->a: %g\n",
		 __FILE__, __LINE__, G_GNUC_PRETTY_FUNCTION,
		 a, GFS_STATE (cell)->solid->a);
	*ret = FALSE;
    }
  }
}

/**
 * gfs_cell_check_solid_fractions:
 * @root: the root #FttCell of the cell tree to check.
 * @solid: a closed, orientable surface defining the solid boundary or %NULL.
 * @is_open: %TRUE if @solid is an "open" surface.
 * 
 * Checks the consistency of the solid fractions of each cell of the
 * cell tree relative to the neighboring solid fractions and to the
 * solid geometry they represent (if @solid is not %NULL).
 *
 * Returns: %TRUE if the solid fractions are consistent, %FALSE otherwise.
 */
gboolean gfs_cell_check_solid_fractions (FttCell * root,
					 GtsSurface * solid,
					 gboolean is_open)
{
  gboolean ret = TRUE;

  g_return_val_if_fail (root != NULL, FALSE);

#if (!FTT_2D)
  if (solid) {
    /* check that the total volume of the union of the solid with the
       domain is equal to the volume fraction of the root cell */
    GtsSurface * domain; 
    GNode * dtree, * stree;
    GtsSurfaceInter * si;
    FttVector p;
    GtsVector size;
    gboolean closed = TRUE;

    domain = gts_surface_new (gts_surface_class (),
			      gts_face_class (),
			      gts_edge_class (),
			      gts_vertex_class ());
    ftt_cell_pos (root, &p);
    cell_size (root, size);
    surface_add_box (domain, 
		     p.x - size[0]/2., p.y - size[1]/2., p.z - size[2]/2., 
		     p.x + size[0]/2., p.y + size[1]/2., p.z + size[2]/2.);
    dtree = gts_bb_tree_surface (domain);
    stree = gts_bb_tree_surface (solid);
    si = gts_surface_inter_new (gts_surface_inter_class (),
				domain, solid, dtree, stree, FALSE, is_open);
    gts_bb_tree_destroy (stree, TRUE);
    gts_bb_tree_destroy (dtree, TRUE);
    g_assert (gts_surface_inter_check (si, &closed));
    
    if (si->edges == NULL) { /* domain and solid do not intersect */
      gts_object_destroy (GTS_OBJECT (si));
      gts_object_destroy (GTS_OBJECT (domain));
      if (GFS_IS_MIXED (root)) {
	gdouble volume = gts_surface_volume (solid);

	if (volume < 0.) volume += ftt_cell_volume (root);
	volume /= ftt_cell_volume (root);
	if (fabs (GFS_STATE (root)->solid->a - volume) >= 1e-6) {
	  g_warning ("file %s: line %d (%s): solid->a: %g volume: %g",
		     __FILE__, __LINE__, G_GNUC_PRETTY_FUNCTION,
		     GFS_STATE (root)->solid->a, volume);
	  ret = FALSE;
	}
      }
    }
    else { /* domain and solid do intersect */
      GtsSurface * sunion = gts_surface_new (gts_surface_class (),
					     gts_face_class (),
					     gts_edge_class (),
					     gts_vertex_class ());
      gdouble volume;

      g_assert (closed);
      gts_surface_inter_boolean (si, sunion, GTS_1_IN_2);
      gts_surface_inter_boolean (si, sunion, GTS_2_IN_1);
      if (gts_surface_is_orientable (sunion)) {
	volume = gts_surface_volume (sunion)/ftt_cell_volume (root);

	if (!GFS_IS_MIXED (root)) {
	  g_warning ("file %s: line %d (%s): cell is not mixed",
		     __FILE__, __LINE__, G_GNUC_PRETTY_FUNCTION);
	  ret = FALSE;
	}
	else if (fabs (GFS_STATE (root)->solid->a - volume) >= 1e-6) {
	  gchar s[80];
	  FILE * fp;
	  
	  sprintf (s, "/tmp/gerris_warning.%d", warning_number++);
	  fp = fopen (s, "wt");
	  if (fp != NULL) {
	    gts_surface_write_oogl (sunion, fp);
	    fclose (fp);
	  }
	  
	  g_warning ("file %s: line %d (%s): solid->a: %g volume: %g. "
		     "Surface has been written in %s.",
		     __FILE__, __LINE__, G_GNUC_PRETTY_FUNCTION,
		     GFS_STATE (root)->solid->a, volume,
		     fp != NULL ? s : "(null)");
	  ret = FALSE;
	}
      }

      gts_object_destroy (GTS_OBJECT (sunion));
      gts_object_destroy (GTS_OBJECT (si));
      gts_object_destroy (GTS_OBJECT (domain));
    }
  }
#endif /* FTT_3D */

  ftt_cell_traverse (root, FTT_POST_ORDER, FTT_TRAVERSE_NON_LEAFS, -1,
		     (FttCellTraverseFunc) check_solid_fractions, &ret);
  return ret & check_area_fractions (root);
}

/**
 * gfs_refine_mixed:
 * @cell: a #FttCell.
 *
 * Returns: %TRUE if @cell is a mixed leaf cell and any of its
 * neighbors is not a leaf cell, %FALSE otherwise (see figure
 * topology.fig).  
 */
gboolean gfs_refine_mixed (const FttCell * cell)
{
  FttCellNeighbors neighbor;
  guint i;

  g_return_val_if_fail (cell != NULL, FALSE);

  if (!GFS_IS_MIXED (cell) || !FTT_CELL_IS_LEAF (cell))
    return FALSE;

  ftt_cell_neighbors (cell, &neighbor);
  for (i = 0; i < FTT_NEIGHBORS; i++)
    if (neighbor.c[i] && !FTT_CELL_IS_LEAF (neighbor.c[i]))
      return TRUE;
  
  return FALSE;
}

static void gfs_cell_set_fraction (FttCell * cell, 
				   GfsVariable * c,
				   gdouble val)
{
  g_return_if_fail (cell != NULL);

  GFS_VARIABLE (cell, c->i) = val;
  if (!FTT_CELL_IS_LEAF (cell)) {
    FttCellChildren child;
    guint i;
 
    ftt_cell_children (cell, &child);
    for (i = 0; i < FTT_CELLS; i++)
      if (child.c[i])
	gfs_cell_set_fraction (child.c[i], c, val);
  }
}

static void set_full_or_empty (FttCell * cell,
			       GNode * tree,
			       gboolean is_open,
			       GfsVariable * c)
{
  FttVector pos;
  GtsPoint * p;

  ftt_cell_pos (cell, &pos);
  p = gts_point_new (gts_point_class (), pos.x, pos.y, pos.z);
  if (gts_point_is_inside_surface (p, tree, is_open))
    gfs_cell_set_fraction (cell, c, 1.);
  else
    gfs_cell_set_fraction (cell, c, 0.);
  gts_object_destroy (GTS_OBJECT (p));
}

static void set_fraction_from_surface (FttCell * cell,
				       GtsBBox * bbox,
				       GtsSurface * s,
				       GNode * stree,
				       gboolean is_open,
				       GfsVariable * c)
{
  GtsSurface * cs; 
  GNode * ctree;
  GtsSurfaceInter * si;
  gboolean closed = TRUE;

  cs = gts_surface_new (gts_surface_class (),
			GTS_FACE_CLASS (gfs_face_class ()),
			gts_edge_class (),
			gts_vertex_class ());
  surface_add_box (cs, 
		   bbox->x1, bbox->y1, bbox->z1,
		   bbox->x2, bbox->y2, bbox->z2);
  ctree = gts_bb_tree_surface (cs);
  si = gts_surface_inter_new (gts_surface_inter_class (),
			      cs, s, ctree, stree, FALSE, is_open);
  g_assert (gts_surface_inter_check (si, &closed));
  if (si->edges == NULL)
    set_full_or_empty (cell, stree, is_open, c);
  else {
    GtsSurface * inter = gts_surface_new (gts_surface_class (),
					  gts_face_class (),
					  gts_edge_class (),
					  gts_vertex_class ());
    GtsVector size;

    g_assert (closed);
    gts_surface_inter_boolean (si, inter, GTS_1_IN_2);
    gts_surface_inter_boolean (si, inter, GTS_2_IN_1);
    bbox_size (bbox, size);
    GFS_VARIABLE (cell, c->i) = gts_surface_volume (inter)/(size[0]*size[1]*size[2]);
    g_assert (GFS_VARIABLE (cell, c->i) > -1e-9 && 
	      GFS_VARIABLE (cell, c->i) < 1. + 1e-9);
    gts_object_destroy (GTS_OBJECT (inter));
  }

  gts_object_destroy (GTS_OBJECT (si));
  gts_bb_tree_destroy (ctree, TRUE);
  gts_object_destroy (GTS_OBJECT (cs));
}

/**
 * gfs_cell_init_fraction:
 * @root: the root #FttCell of the cell tree.
 * @s: a closed, orientable surface defining the solid boundary.
 * @stree: a bounding box tree of the faces of @s.
 * @is_open: %TRUE if @s is an "open" boundary i.e. the signed volume
 * enclosed by @s is negative, %FALSE otherwise.
 * @c: a #GfsVariable.
 *
 * Initializes the fraction @c of all the cells of the cell tree
 * starting at @root.
 */
void gfs_cell_init_fraction (FttCell * root, 
			     GtsSurface * s,
			     GNode * stree,
			     gboolean is_open,
			     GfsVariable * c)
{
  GtsBBox * bbox;

  g_return_if_fail (root != NULL);
  g_return_if_fail (s != NULL);
  g_return_if_fail (stree != NULL);
  g_return_if_fail (c != NULL);

  bbox = bbox_cell (gts_bbox_class (), root);
  if (gts_bb_tree_is_overlapping (stree, bbox)) {
    if (FTT_CELL_IS_LEAF (root))
      set_fraction_from_surface (root, bbox, s, stree, is_open, c);
    else {
      FttCellChildren child;
      guint i;

      ftt_cell_children (root, &child);
      for (i = 0; i < FTT_CELLS; i++)
	if (child.c[i])
	  gfs_cell_init_fraction (child.c[i], s, stree, is_open, c);
      gfs_get_from_below_extensive (root, c);
      GFS_VARIABLE (root, c->i) /= 4.;
    }
  }
  else /* !gts_bb_tree_is_overlapping (stree, bbox) */
    set_full_or_empty (root, stree, is_open, c);

  gts_object_destroy (GTS_OBJECT (bbox));
}

/**
 * gfs_cell_cm:
 * @cell: a #FttCell.
 * @cm: a #FttVector.
 *
 * Fills @cm with the coordinates of the center of mass of @cell.
 */
void gfs_cell_cm (const FttCell * cell, FttVector * cm)
{
  g_return_if_fail (cell != NULL);
  g_return_if_fail (cm != NULL);

  if (GFS_IS_MIXED (cell))
    *cm = GFS_STATE (cell)->solid->cm;
  else
    ftt_cell_pos (cell, cm);
}

/**
 * gfs_face_ca:
 * @face: a #FttCellFace.
 * @ca: a #FttVector.
 *
 * Fills @ca with the coordinates of the center of area of @face.
 */
void gfs_face_ca (const FttCellFace * face, FttVector * ca)
{
  gdouble f;

  g_return_if_fail (face != NULL);
  g_return_if_fail (ca != NULL);

  ftt_face_pos (face, ca);
  if ((f = GFS_FACE_FRACTION (face)) < 1.) {
    GfsSolidVector * s = GFS_STATE (face->cell)->solid;
    gdouble h = ftt_cell_size (face->cell);
#if FTT_2D
    FttComponent cp = FTT_ORTHOGONAL_COMPONENT (face->d/2);

    (&ca->x)[cp] += (s->s[2*cp] > s->s[2*cp + 1]) ? (1. - f)/2.*h : (f - 1.)/2.*h;
#else /* 3D */
    static guint perpendicular[FTT_DIMENSION][2] = {
      {FTT_Y, FTT_Z}, {FTT_Z, FTT_X}, {FTT_X, FTT_Y}
    };
    FttComponent c0 = face->d/2;
    FttComponent c1 = perpendicular[c0][0];
    FttComponent c2 = perpendicular[c0][1];
    gboolean s1, s2;
    FttVector m, p;
    gdouble n, alpha;

    m.x = s->s[2*c1 + 1] - s->s[2*c1];
    m.y = s->s[2*c2 + 1] - s->s[2*c2];
    s1 = (m.x < 0.);
    s2 = (m.y < 0.);
    m.x = fabs (m.x) + 1e-6;
    m.y = fabs (m.y) + 1e-6;
    n = m.x + m.y;
    m.x /= n;
    m.y /= n;
    alpha = gfs_line_alpha (&m, f);
    gfs_line_center (&m, alpha, f, &p);
    if (s1) p.x = 1. - p.x;
    if (s2) p.y = 1. - p.y;
    (&ca->x)[c1] += (p.x - 0.5)*h;
    (&ca->x)[c2] += (p.y - 0.5)*h;
#endif /* 3D */
  }
}
