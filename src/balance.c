/* Gerris - The GNU Flow Solver
 * Copyright (C) 2001-2009 National Institute of Water and Atmospheric Research
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

#include "balance.h"
#include "mpi_boundary.h"
#include "adaptive.h"

/* GfsEventBalance: Object */

#ifdef HAVE_MPI

static void find_neighbors (GfsBox * box, GArray * pe)
{
  FttDirection d;
  for (d = 0; d < FTT_NEIGHBORS; d++)
    if (GFS_IS_BOUNDARY_MPI (box->neighbor[d])) {
      guint process = GFS_BOUNDARY_MPI (box->neighbor[d])->process;
      gboolean found = FALSE;
      gint i;
      for (i = 0; i < pe->len && !found; i++)
	if (g_array_index (pe, guint, i) == process)
	  found = TRUE;
      if (!found)
	g_array_append_val (pe, process);
    }      
}

static GArray * neighboring_processors (GfsDomain * domain)
{
  GArray * pe = g_array_new (FALSE, FALSE, sizeof (guint));
  if (domain->pid >= 0)
    gts_container_foreach (GTS_CONTAINER (domain), (GtsFunc) find_neighbors, pe);
  return pe;
}

static void count (FttCell * cell, int * n)
{
  (*n)++;
}

#define NITERMAX 100
#define TOL 0.001

typedef struct {
  guint * pid;     /* pid of neighbors */
  gdouble * flow;  /* flow to neighbors */
  guint n;         /* number of neighbors */
} BalancingFlow;

/*
 * Computes the "balancing flow" necessary to balance the domain
 * sizes on all the processes. @average is the average domain size
 * (i.e. the target domain size).
 */
static BalancingFlow * balancing_flow_new (GfsDomain * domain, int average)
{
  BalancingFlow * b;

  b = g_malloc0 (sizeof (BalancingFlow));
  GArray * pe = neighboring_processors (domain);
  if (pe->len == 0) {
    g_array_free (pe, TRUE);
    return b;
  }
  int size = 0, i;
  gfs_domain_traverse_leaves (domain, (FttCellTraverseFunc) count, &size);
  gdouble rsize = size - average;
  gdouble * lambda = g_malloc (sizeof (gdouble)*(pe->len + 1)), lambda1, eps = G_MAXDOUBLE;
  MPI_Request * request = g_malloc (sizeof (MPI_Request)*pe->len);
  lambda1 = 0.;
  /* this is Gamma and s from the "double-loop" fix of Johnson,
     Bickson and Dolev, 2009, equation (6) */
  gdouble Gamma = 0.5*pe->len, s = 0.5;
  gdouble tolerance = MAX (TOL*average, 1.);
  int niter = NITERMAX;
  while (niter-- && eps > tolerance) {
    MPI_Status status;
    /* Send lambda to all neighbors */
    lambda[0] = lambda1;
    for (i = 0; i < pe->len; i++)
      MPI_Isend (&(lambda[0]), 1, MPI_DOUBLE, g_array_index (pe, guint , i), 
		 domain->pid,
		 MPI_COMM_WORLD,
		 &(request[i]));
    /* Do one iteration of the "double-loop"-fixed Jacobi method for
       the (graph)-Poisson equation */
    gdouble rhs = rsize;
    for (i = 0; i < pe->len; i++) {
      MPI_Recv (&(lambda[i + 1]), 1, MPI_DOUBLE, g_array_index (pe, guint , i), 
		g_array_index (pe, guint , i),
		MPI_COMM_WORLD, &status);
      rhs += lambda[i + 1];
    }
    /* update RHS and lambda for "double-loop"-fix */
    rsize = (1. - s)*rsize + s*(size - average + Gamma*lambda[0]);
    lambda1 = rhs/(Gamma + pe->len);
    eps = fabs (lambda[0] - lambda1);
    /* synchronize */
    for (i = 0; i < pe->len; i++)
      MPI_Wait (&request[i], &status);
    gfs_all_reduce (domain, eps, MPI_DOUBLE, MPI_MAX);
  }
  g_free (request);
  if (niter < 0 && domain->pid == 0)
    g_warning ("balancing_flow(): could not converge after %d iterations", NITERMAX);

  b->n = pe->len;
  lambda1 = lambda[0];
  for (i = 0; i < b->n; i++)
    lambda[i] = lambda1 - lambda[i + 1];
  b->flow = lambda;
  b->pid = (guint *) g_array_free (pe, FALSE);
  return b;
}

static void balancing_flow_destroy (BalancingFlow * b)
{
  g_free (b->pid);
  g_free (b->flow);
  g_free (b);
}

typedef struct {
  GfsBox * box;
  gint dest, n, min, neighboring;
} BoxData;

static void select_neighbouring_box (GfsBox * box, BoxData * b)
{
  gint neighboring = 0;
  FttDirection d;

  for (d = 0; d < FTT_NEIGHBORS; d++)
    if (GFS_IS_BOUNDARY_MPI (box->neighbor[d]) &&
	GFS_BOUNDARY_MPI (box->neighbor[d])->process == b->dest)
      neighboring++;

  if (neighboring && neighboring >= b->neighboring) {
    box->size = 0;
    ftt_cell_traverse (box->root, FTT_PRE_ORDER, FTT_TRAVERSE_LEAFS, -1,
		       (FttCellTraverseFunc) count, &(box->size));
    if (neighboring > b->neighboring ||
	fabs (box->size - b->n) < fabs (b->box->size - b->n)) {
      b->box = box;
      b->neighboring = neighboring;
    }
  }
}

static void get_pid (GfsBox * box, guint * pid)
{
  pid[box->id - 1] = gfs_box_domain (box)->pid;
}

static void update_box_pid (GfsBox * box, guint * pid)
{
  FttDirection d;
  for (d = 0; d < FTT_NEIGHBORS; d++)
    if (GFS_IS_BOUNDARY_MPI (box->neighbor[d]))
      GFS_BOUNDARY_MPI (box->neighbor[d])->process = 
	pid[GFS_BOUNDARY_MPI (box->neighbor[d])->id - 1];
}

#endif /* HAVE_MPI */

static void gfs_event_balance_write (GtsObject * o, FILE * fp)
{
  GfsEventBalance * s = GFS_EVENT_BALANCE (o);

  if (GTS_OBJECT_CLASS (gfs_event_balance_class ())->parent_class->write)
    (* GTS_OBJECT_CLASS (gfs_event_balance_class ())->parent_class->write)
      (o, fp);

  fprintf (fp, " %g", s->max);
}

static void gfs_event_balance_read (GtsObject ** o, GtsFile * fp)
{
  GfsEventBalance * s = GFS_EVENT_BALANCE (*o);
  GfsDomain * domain =  GFS_DOMAIN (gfs_object_simulation (s));

  if (GTS_OBJECT_CLASS (gfs_event_balance_class ())->parent_class->read)
    (* GTS_OBJECT_CLASS (gfs_event_balance_class ())->parent_class->read) 
      (o, fp);
  if (fp->type == GTS_ERROR)
    return;
  
  s->max = gfs_read_constant (fp, domain);
}

static gboolean gfs_event_balance_event (GfsEvent * event, GfsSimulation * sim)
{
  if ((* GFS_EVENT_CLASS (GTS_OBJECT_CLASS (gfs_event_balance_class ())->parent_class)->event) 
      (event, sim)) {
    GfsDomain * domain = GFS_DOMAIN (sim);
    GfsEventBalance * s = GFS_EVENT_BALANCE (event);
    GtsRange size, boundary, mpiwait;

    gfs_domain_stats_balance (domain, &size, &boundary, &mpiwait);
    if (size.max/size.min > 1. + s->max) {
#ifdef HAVE_MPI
      BalancingFlow * balance = balancing_flow_new (domain, size.mean);
      GPtrArray * request = g_ptr_array_new ();
      int modified = FALSE;
      int i;
      /* Send boxes */
      for (i = 0; i < balance->n; i++)
	if (balance->flow[i] > 0.) { /* largest subdomain */
	  GSList * l = NULL;
	  if (gts_container_size (GTS_CONTAINER (domain)) > 1) {
	    BoxData b;
	    b.box = NULL; b.neighboring = 0; b.n = balance->flow[i];
	    b.dest = balance->pid[i];
	    /* we need to find the list of boxes which minimizes 
	       |\sum n_i - n| where n_i is the size of box i. This is known in
	       combinatorial optimisation as a "knapsack problem". */
	    gts_container_foreach (GTS_CONTAINER (domain), (GtsFunc) select_neighbouring_box, &b);
	    if (b.box && b.box->size <= 2*b.n) {
	      l = g_slist_prepend (l, b.box);
	      modified = TRUE;
	    }
	  }
	  g_ptr_array_add (request, gfs_send_boxes (domain, l, balance->pid[i]));
	  g_slist_free (l);
	}
      /* Receive boxes */
      for (i = 0; i < balance->n; i++)
	if (balance->flow[i] < 0.) /* smallest subdomain */
	  g_slist_free (gfs_receive_boxes (domain, balance->pid[i]));
      /* Synchronize */
      for (i = 0; i < request->len; i++)
	gfs_wait (g_ptr_array_index (request, i));
      g_ptr_array_free (request, TRUE);
      balancing_flow_destroy (balance);
      /* Reshape */
      gfs_all_reduce (domain, modified, MPI_INT, MPI_MAX);
      if (modified) {
	/* Updates the pid associated with each box */
	guint nb = gts_container_size (GTS_CONTAINER (domain));
	gfs_all_reduce (domain, nb, MPI_UNSIGNED, MPI_SUM);
	guint * pid = g_malloc0 (sizeof (guint)*nb);
	gts_container_foreach (GTS_CONTAINER (domain), (GtsFunc) get_pid, pid);
	MPI_Allreduce (pid, pid, nb, MPI_UNSIGNED, MPI_MAX, MPI_COMM_WORLD);
	/* pid[id] now contains the current pid of box with index id */
	gts_container_foreach (GTS_CONTAINER (domain), (GtsFunc) update_box_pid, pid);
	g_free (pid);
	gfs_domain_reshape (domain, gfs_domain_depth (domain));
      }
#else /* not HAVE_MPI */
      g_assert_not_reached ();
#endif /* not HAVE_MPI */
    }
    return TRUE;
  }
  return FALSE;
}

static void gfs_event_balance_class_init (GfsEventClass * klass)
{
  GTS_OBJECT_CLASS (klass)->read = gfs_event_balance_read;
  GTS_OBJECT_CLASS (klass)->write = gfs_event_balance_write;
  GFS_EVENT_CLASS (klass)->event = gfs_event_balance_event;
}

GfsEventClass * gfs_event_balance_class (void)
{
  static GfsEventClass * klass = NULL;

  if (klass == NULL) {
    GtsObjectClassInfo gfs_event_balance_info = {
      "GfsEventBalance",
      sizeof (GfsEventBalance),
      sizeof (GfsEventClass),
      (GtsObjectClassInitFunc) gfs_event_balance_class_init,
      (GtsObjectInitFunc) NULL,
      (GtsArgSetFunc) NULL,
      (GtsArgGetFunc) NULL
    };
    klass = gts_object_class_new (GTS_OBJECT_CLASS (gfs_event_class ()),
				  &gfs_event_balance_info);
  }

  return klass;
}