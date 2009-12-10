/* Gerris - The GNU Flow Solver			(-*-C-*-)
 * Copyright (C) 2009 National Institute of Water and Atmospheric Research
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
#include "particle.h"
#include "source.h"

/* GfsParticulate: Header */

typedef struct _GfsParticulate GfsParticulate;

struct _GfsParticulate {
  GfsParticle parent;
  FttVector vel;
  gdouble mass, volume;
  FttVector force;
  GtsSListContainer * forces;
};

#define GFS_PARTICULATE(obj)            GTS_OBJECT_CAST (obj,		\
							 GfsParticulate, gfs_particulate_class ())
#define GFS_IS_PARTICULATE(obj)         (gts_object_is_from_class (obj, gfs_particulate_class ()))

static GfsEventClass * gfs_particulate_class  (void);

/* GfsParticleList: Header */

typedef struct _GfsParticleList GfsParticleList;

struct _GfsParticleList {
  GfsEventList parent;
  gint idlast;
  GtsSListContainer * forces;
};

#define GFS_PARTICLE_LIST(obj)            GTS_OBJECT_CAST (obj,		\
							   GfsParticleList, \
							   gfs_particle_list_class ())

#define GFS_IS_PARTICLE_LIST(obj)         (gts_object_is_from_class (obj, \
								     gfs_particle_list_class ()))

static GfsEventClass * gfs_particle_list_class  (void);

/* GfsParticleForce: header */

typedef struct _GfsParticleForce GfsParticleForce;

struct _GfsParticleForce{
  GtsSListContainee parent;
  FttVector (* force) (GfsParticle *p, GfsParticleForce *force);
};

#define GFS_PARTICLE_FORCE(obj)            GTS_OBJECT_CAST (obj,		\
							GfsParticleForce, \
							gfs_particle_force_class ())
#define GFS_IS_PARTICLE_FORCE(obj)         (gts_object_is_from_class (obj, \
								      gfs_particle_force_class ()))

static GtsSListContaineeClass * gfs_particle_force_class  (void);

/* ForceCoeff: header */

typedef struct _ForceCoeff ForceCoeff;

struct _ForceCoeff{
  GfsParticleForce parent;
  GfsFunction * coefficient;
  GfsVariable *re_p, *u_rel, *v_rel, *w_rel, *pdia;
  GfsParticulate *p;
};

#define FORCE_COEFF(obj)            GTS_OBJECT_CAST (obj,		\
						    ForceCoeff,		\
						    gfs_force_coeff_class ())
#define GFS_IS_FORCE_COEFF(obj)         (gts_object_is_from_class (obj,	\
								  gfs_force_coeff_class ()))
static GtsSListContaineeClass * gfs_force_coeff_class  (void);

/* ForceLift: header */

#define GFS_IS_FORCE_LIFT(obj)         (gts_object_is_from_class (obj,	\
								  gfs_force_lift_class ()))
static GtsSListContaineeClass * gfs_force_lift_class  (void);

/* ForceDrag: header */

#define GFS_IS_FORCE_DRAG(obj)         (gts_object_is_from_class (obj,	\
								  gfs_force_drag_class ()))
static GtsSListContaineeClass * gfs_force_drag_class  (void);

/* ForceBuoy: header */

#define GFS_IS_FORCE_BUOY(obj)         (gts_object_is_from_class (obj,	\
								  gfs_force_buoy_class ()))
static GtsSListContaineeClass * gfs_force_buoy_class  (void);

/* Forces on the Particle */

static FttVector subs_fttvectors (FttVector *a, FttVector *b)
{
  FttVector result;
  FttComponent c;
  for(c = 0; c< FTT_DIMENSION; c++)    
    (&result.x)[c]  = (&a->x)[c] - (&b->x)[c];  
  return result;
}

/* Same as in source.c used here to obtained viscosity */
static GfsSourceDiffusion * source_diffusion_viscosity (GfsVariable * v)
{
  if (v->sources) {
    GSList * i = GTS_SLIST_CONTAINER (v->sources)->items;
    
    while (i) {
      GtsObject * o = i->data;
      
      if (GFS_IS_SOURCE_DIFFUSION (o))
	return GFS_SOURCE_DIFFUSION (o);
      i = i->next;
    }
  }
  return NULL;
}

/* Similar to gfs_vorticity which returns norm of the vorticity */
static void vorticity_vector (FttCell *cell, GfsVariable **v, 
			      FttVector *vort) 
{
  gdouble size;

  if (cell == NULL) return;
  if (v == NULL) return;

  size = ftt_cell_size (cell);
#if FTT_2D
  vort->x = 0.;
  vort->y = 0.;
  vort->z = (gfs_center_gradient (cell, FTT_X, v[1]->i) -
	     gfs_center_gradient (cell, FTT_Y, v[0]->i))/size;
#else  /* FTT_3D */
  vort->x = (gfs_center_gradient (cell, FTT_Y, v[2]->i) -
	     gfs_center_gradient (cell, FTT_Z, v[1]->i))/size;
  vort->y = (gfs_center_gradient (cell, FTT_Z, v[0]->i) -
	     gfs_center_gradient (cell, FTT_X, v[2]->i))/size;
  vort->z = (gfs_center_gradient (cell, FTT_X, v[1]->i) -
	     gfs_center_gradient (cell, FTT_Y, v[0]->i))/size;
#endif
}

/* ForceCoeff: object */

static void gfs_force_coeff_read (GtsObject ** o, GtsFile * fp)
{
  if (GTS_OBJECT_CLASS (gfs_force_coeff_class ())->parent_class->read)
    (* GTS_OBJECT_CLASS (gfs_force_coeff_class ())->parent_class->read) 
      (o, fp);
  if (fp->type == GTS_ERROR)
    return;

  if (fp->type != '\n') {
    ForceCoeff * force = FORCE_COEFF (*o);
    force->coefficient = gfs_function_new (gfs_function_class (), 0.);
    gfs_function_read (force->coefficient, gfs_object_simulation (*o), fp);
    GfsDomain * domain = GFS_DOMAIN (gfs_object_simulation (*o));
    
    /* fixme: "Rep", "Urelp" etc... should be derived variables not
       straight variables (i.e. there is no need to allocate memory
       for these as they are only used temporarily to compute the
       coefficient) */
    force->re_p = gfs_domain_get_or_add_variable (domain, "Rep", 
						  "Particle Reynolds number");  
    force->u_rel = gfs_domain_get_or_add_variable (domain, "Urelp", 
						   "Particle x - relative velocity");
    force->v_rel = gfs_domain_get_or_add_variable (domain, "Vrelp", 
						   "Particle y - relative velocity");
#if !FTT_2D
    force->w_rel = gfs_domain_get_or_add_variable (domain, "Wrelp", 
						   "Particle z - relative velocity");
#endif
    force->pdia = gfs_domain_get_or_add_variable (domain, "Pdia", 
						  "Particle radii");
  }
}

static void gfs_force_coeff_write (GtsObject * o, FILE * fp)
{
  (* GTS_OBJECT_CLASS (gfs_force_coeff_class ())->parent_class->write) (o, fp);
  ForceCoeff * force = FORCE_COEFF (o);
  if (force->coefficient)
    gfs_function_write (force->coefficient, fp);
}

static void gfs_force_coeff_destroy (GtsObject * o)
{
  if (FORCE_COEFF (o)->coefficient)
    gts_object_destroy (GTS_OBJECT (FORCE_COEFF (o)->coefficient));

  (* GTS_OBJECT_CLASS (gfs_force_coeff_class ())->parent_class->destroy) (o);
}

static void gfs_force_coeff_class_init (GtsObjectClass * klass)
{
  klass->read = gfs_force_coeff_read;
  klass->write = gfs_force_coeff_write;
  klass->destroy = gfs_force_coeff_destroy;
}
 
GtsSListContaineeClass * gfs_force_coeff_class (void)
{
  static GtsSListContaineeClass * klass = NULL;
  
  if (klass == NULL) {
    GtsObjectClassInfo gfs_force_coeff_info = {
      "ForceCoeff",
      sizeof (ForceCoeff),
      sizeof (GtsSListContaineeClass),
      (GtsObjectClassInitFunc) gfs_force_coeff_class_init,
      (GtsObjectInitFunc) NULL,
      (GtsArgSetFunc) NULL,
      (GtsArgGetFunc) NULL
    };
    klass = gts_object_class_new (GTS_OBJECT_CLASS (gfs_particle_force_class ()),
				  &gfs_force_coeff_info);
  }
  return klass;
}

/* ForceLift: object */

static FttVector compute_lift_force (GfsParticle * p, GfsParticleForce * liftforce)
{
  GfsParticulate * particulate = GFS_PARTICULATE (p);
  ForceCoeff * coeff = FORCE_COEFF (liftforce);

  GfsSimulation * sim = gfs_object_simulation (particulate);
  GfsDomain * domain = GFS_DOMAIN (sim);
  
  FttVector force;
  FttComponent c;
  for (c = 0; c < FTT_DIMENSION; c++)
    (&force.x)[c] = 0;

  FttCell * cell = gfs_domain_locate (domain, p->pos, -1, NULL);
  if (cell == NULL) return force;

  gdouble fluid_rho = sim->physical_params.alpha ? 1./
    gfs_function_value (sim->physical_params.alpha, cell) : 1.;
  GfsVariable ** u = gfs_domain_velocity (domain);
 
  gdouble viscosity = 0.;
  GfsSourceDiffusion * d = source_diffusion_viscosity (u[0]); 
  if (d) viscosity = gfs_diffusion_cell (d->D, cell);
  
  FttVector fluid_vel;
  for (c = 0; c < FTT_DIMENSION; c++)
    (&fluid_vel.x)[c] = gfs_interpolate (cell, p->pos, u[c]);

  FttVector relative_vel = subs_fttvectors (&fluid_vel, &particulate->vel);
  FttVector vorticity;
  vorticity_vector (cell, u, &vorticity);

  gdouble cl = 0.5;
  if (coeff->coefficient) {
    gdouble norm_relative_vel = sqrt (relative_vel.x*relative_vel.x + 
				      relative_vel.y*relative_vel.y +
				      relative_vel.z*relative_vel.z);
    gdouble dia =  2.*pow(3.0*(particulate->volume)/4.0/M_PI, 1./3.);    
    gdouble Re = norm_relative_vel*dia*fluid_rho/viscosity;

    GFS_VALUE (cell, coeff->re_p) = Re;
    GFS_VALUE (cell, coeff->pdia) = dia;
    GFS_VALUE (cell, coeff->u_rel) = relative_vel.x;
    GFS_VALUE (cell, coeff->v_rel) = relative_vel.y;
#if !FTT_2D
    GFS_VALUE (cell, coeff->w_rel) = relative_vel.z;
#endif
    cl = gfs_function_value (coeff->coefficient, cell); 
  }
 
#if FTT_2D
  force.x = fluid_rho*cl*relative_vel.y*vorticity.z;
  force.y = -fluid_rho*cl*relative_vel.x*vorticity.z;
#else
  force.x = fluid_rho*cl*(relative_vel.y*vorticity.z
			  -relative_vel.z*vorticity.y);
  force.y = fluid_rho*cl*(relative_vel.z*vorticity.x
			  -relative_vel.x*vorticity.z);
  force.z = fluid_rho*cl*(relative_vel.x*vorticity.y
			  -relative_vel.y*vorticity.x);
#endif

  return force; 
}

static void gfs_force_lift_init (GfsParticleForce * force)
{
  force->force = compute_lift_force;
}

GtsSListContaineeClass * gfs_force_lift_class (void)
{
  static GtsSListContaineeClass * klass = NULL;
  
  if (klass == NULL) {
    GtsObjectClassInfo gfs_force_lift_info = {
      "ForceLift",
      sizeof (ForceCoeff),
      sizeof (GtsSListContaineeClass),
      (GtsObjectClassInitFunc) NULL,
      (GtsObjectInitFunc) gfs_force_lift_init,
      (GtsArgSetFunc) NULL,
      (GtsArgGetFunc) NULL
    };
    klass = gts_object_class_new (GTS_OBJECT_CLASS (gfs_force_coeff_class ()),
				  &gfs_force_lift_info);
  }
  return klass;
}

/* ForceDrag: object */

static FttVector compute_drag_force (GfsParticle * p, GfsParticleForce * dragforce)
{
  GfsParticulate * particulate = GFS_PARTICULATE (p);
  ForceCoeff * coeff = FORCE_COEFF (dragforce);
  GfsSimulation * sim = gfs_object_simulation (particulate);
  GfsDomain * domain = GFS_DOMAIN (sim);

  FttVector force;
  FttComponent c;
  for (c = 0; c < FTT_DIMENSION; c++)
    (&force.x)[c] = 0;

  FttCell * cell = gfs_domain_locate (domain, p->pos, -1, NULL);
  if (cell == NULL) return force;

  gdouble fluid_rho = sim->physical_params.alpha ? 1./
    gfs_function_value (sim->physical_params.alpha,cell) : 1.;
  GfsVariable ** u = gfs_domain_velocity (domain);  

  gdouble viscosity = 0.;  

  GfsSourceDiffusion * d = source_diffusion_viscosity (u[0]); 
  if (d) viscosity = gfs_diffusion_cell (d->D, cell);
  
  FttVector fluid_vel;
  for (c = 0; c < FTT_DIMENSION; c++)
    (&fluid_vel.x)[c] = gfs_interpolate (cell, p->pos, u[c]);

  FttVector relative_vel = subs_fttvectors (&fluid_vel, &particulate->vel);

  gdouble dia = 2.*pow(3.0*(particulate->volume)/4.0/M_PI, 1./3.);
#if !FTT_2D
  gdouble norm_relative_vel = sqrt (relative_vel.x*relative_vel.x + 
				    relative_vel.y*relative_vel.y +
				    relative_vel.z*relative_vel.z);
#else
  gdouble norm_relative_vel = sqrt (relative_vel.x*relative_vel.x + 
				    relative_vel.y*relative_vel.y);
#endif

  gdouble cd = 0.;
  gdouble Re;
  if (viscosity == 0)    
    return force;
  else
    Re = norm_relative_vel*dia*fluid_rho/viscosity;
  
  if (coeff->coefficient) {
    GFS_VALUE (cell, coeff->re_p) = Re;
    GFS_VALUE (cell, coeff->u_rel) = relative_vel.x;
    GFS_VALUE (cell, coeff->v_rel) = relative_vel.y;
#if !FTT_2D
    GFS_VALUE (cell, coeff->w_rel) = relative_vel.z;
#endif
    GFS_VALUE (cell, coeff->pdia) = dia;
    cd = gfs_function_value (coeff->coefficient, cell); 
  }
  else {
    if (Re < 1e-8)
      return force;
    else if (Re < 50.0)
      cd = 16.*(1. + 0.15*pow(Re,0.5))/Re;
    else
      cd = 48.*(1. - 2.21/pow(Re,0.5))/Re;
  }
  for (c = 0; c < FTT_DIMENSION; c++)
    (&force.x)[c] += 3./(4.*dia)*cd*norm_relative_vel*(&relative_vel.x)[c]*fluid_rho;
  
  return force;
}

static void gfs_force_drag_init (GfsParticleForce * force)
{
  force->force = compute_drag_force;
}

static GtsSListContaineeClass * gfs_force_drag_class (void)
{
  static GtsSListContaineeClass * klass = NULL;
  
  if (klass == NULL) {
    GtsObjectClassInfo gfs_force_drag_info = {
      "ForceDrag",
      sizeof (ForceCoeff),
      sizeof (GtsSListContaineeClass),
      (GtsObjectClassInitFunc) NULL,
      (GtsObjectInitFunc) gfs_force_drag_init,
      (GtsArgSetFunc) NULL,
      (GtsArgGetFunc) NULL
    };
    klass = gts_object_class_new (GTS_OBJECT_CLASS (gfs_force_coeff_class ()),
				  &gfs_force_drag_info);
  }
  return klass;
}

/* ForceBuoy: object */

static FttVector compute_buoyancy_force (GfsParticle * p, GfsParticleForce * buoyforce)
{
  GfsParticulate * particulate = GFS_PARTICULATE (p); 
  GfsSimulation * sim = gfs_object_simulation (particulate);
  GfsDomain * domain = GFS_DOMAIN (sim);

  FttVector force;
  FttComponent c;
  for (c = 0; c < FTT_DIMENSION; c++)
    (&force.x)[c] = 0;

  FttCell * cell = gfs_domain_locate (domain, p->pos, -1, NULL);
  if (cell == NULL) return force;

  gdouble fluid_rho = sim->physical_params.alpha ? 1./
    gfs_function_value (sim->physical_params.alpha, cell) : 1.;
  GfsVariable ** u = gfs_domain_velocity (domain);
 
  gdouble g[3];
  for (c = 0; c < FTT_DIMENSION; c++) {
    g[c] = 0.;
    if (u[c]->sources) {
      GSList * i = GTS_SLIST_CONTAINER (u[c]->sources)->items;
      
      while (i) {
	if (GFS_IS_SOURCE (i->data)) {
	  g[c] += gfs_function_value (GFS_SOURCE ((GfsSourceGeneric *) i->data)->intensity, 
				      cell);
	}
	i = i->next;
      }
    }
  }

  for (c = 0; c < FTT_DIMENSION; c++)
    (&force.x)[c] += (particulate->mass/particulate->volume-fluid_rho)*g[c];

  return force;
}

static void gfs_force_buoy_init (GfsParticleForce * force)
{
  force->force = compute_buoyancy_force;
}

static GtsSListContaineeClass * gfs_force_buoy_class (void)
{
  static GtsSListContaineeClass * klass = NULL;
  
  if (klass == NULL) {
    GtsObjectClassInfo gfs_force_buoy_info = {
      "ForceBuoy",
      sizeof (GfsParticleForce),
      sizeof (GtsSListContaineeClass), 
      (GtsObjectClassInitFunc) NULL, 
      (GtsObjectInitFunc) gfs_force_buoy_init,
      (GtsArgSetFunc) NULL,
      (GtsArgGetFunc) NULL
    };
    klass = gts_object_class_new (GTS_OBJECT_CLASS (gfs_particle_force_class ()),
				  &gfs_force_buoy_info);
  }
  return klass;
}

/* GfsParticleForce: object */

static void gfs_particle_force_read (GtsObject ** o, GtsFile * fp)
{ 
  GtsObjectClass *klass;

  if (fp->type != GTS_STRING) {
    gts_file_error (fp, "expecting a string (GfsParticleClass)");
    return;
  }

  klass = gfs_object_class_from_name (fp->token->str);
  if (klass == NULL) {
    gts_file_error (fp, "unknown class `%s'", fp->token->str);
    return;
  }
  if (!gts_object_class_is_from_class (klass, gfs_particle_force_class ())) {
    gts_file_error (fp, "`%s' is not a GfsParticleForce", fp->token->str);
    return;
  }
  gts_file_next_token (fp);
}

static void gfs_particle_force_write (GtsObject * o, FILE * fp)
{
  fprintf (fp, "%s", o->klass->info.name);
}

static void gfs_particle_force_class_init (GtsObjectClass * klass)
{
  GTS_OBJECT_CLASS(klass)->read = gfs_particle_force_read;
  GTS_OBJECT_CLASS(klass)->write = gfs_particle_force_write;
}

GtsSListContaineeClass * gfs_particle_force_class (void)
{
  static GtsSListContaineeClass * klass = NULL;

  if (klass == NULL) {
    GtsObjectClassInfo gfs_particle_force_info = {
      "GfsParticleForce",
      sizeof (GfsParticleForce),
      sizeof (GtsSListContaineeClass),
      (GtsObjectClassInitFunc) gfs_particle_force_class_init,
      (GtsArgSetFunc) NULL,
      (GtsArgGetFunc) NULL
    };
    klass = gts_object_class_new (GTS_OBJECT_CLASS (gts_slist_containee_class ()),
				  &gfs_particle_force_info);
  }
  return klass;
}

/* GfsParticulate: Object */

static void compute_forces (GfsParticleForce * event, GfsParticulate * p)
{ 
  p->force = (event->force) (GFS_PARTICLE (p), event); 
}

static gboolean gfs_particulate_event (GfsEvent * event, 
				       GfsSimulation * sim)
{
  if ((* GFS_EVENT_CLASS (GTS_OBJECT_CLASS (gfs_particulate_class ())->parent_class)->event)
      (event, sim)) {
    GfsParticle * p = GFS_PARTICLE (event);
    GfsParticulate * particulate = GFS_PARTICULATE (event);
    FttVector pos = p->pos;
    gfs_simulation_map (sim, &pos);
 
    FttComponent c;
    /* Velocity Verlet Algorithm */
    for (c = 0; c < FTT_DIMENSION; c++) {
      (&pos.x)[c] += (&particulate->force.x)[c]*sim->advection_params.dt*sim->advection_params.dt
	/particulate->mass/2.;
      (&particulate->vel.x)[c] += (&particulate->force.x)[c]*sim->advection_params.dt
	/(2.*particulate->mass);
    }
      
    /* Compute forces */
    if (particulate->forces != NULL) {
      for (c = 0; c < FTT_DIMENSION; c++)
	(&particulate->force.x)[c] = 0.;      
      gts_container_foreach (GTS_CONTAINER (particulate->forces), 
			     (GtsFunc) compute_forces, particulate);
    }
    
    for (c = 0; c < FTT_DIMENSION; c++)
      (&particulate->vel.x)[c] += 
	(&particulate->force.x)[c]*sim->advection_params.dt/(2.*particulate->mass);
    
    gfs_simulation_map_inverse (sim, &pos);
    p->pos = pos;   
    return TRUE;
  }
  return FALSE;
} 

static void gfs_particulate_read (GtsObject ** o, GtsFile * fp)
{
  if (GTS_OBJECT_CLASS (gfs_particulate_class ())->parent_class->read)
    (* GTS_OBJECT_CLASS (gfs_particulate_class ())->parent_class->read) 
      (o, fp);
  if (fp->type == GTS_ERROR)
    return;
  GfsParticulate * p = GFS_PARTICULATE (*o);

  if (fp->type != GTS_INT && fp->type != GTS_FLOAT) {
    gts_file_error (fp, "expecting a number (mass)");
    return;
  }
  p->mass = atof (fp->token->str);
  gts_file_next_token (fp);
  
  if (fp->type != GTS_INT && fp->type != GTS_FLOAT) {
    gts_file_error (fp, "expecting a number (volume)");
    return;
  }
  p->volume = atof (fp->token->str);
  gts_file_next_token (fp);

  if (fp->type != GTS_INT && fp->type != GTS_FLOAT) {
    gts_file_error (fp, "expecting a number (v.x)");
    return;
  }
  p->vel.x = atof (fp->token->str);
  gts_file_next_token (fp);
  
  if (fp->type != GTS_INT && fp->type != GTS_FLOAT) {
    gts_file_error (fp, "expecting a number (v.y)");
    return;
  }
  p->vel.y = atof (fp->token->str);
  gts_file_next_token (fp);
  
  if (fp->type != GTS_INT && fp->type != GTS_FLOAT) {
    gts_file_error (fp, "expecting a number (v.z)");
    return;
  }
  p->vel.z = atof (fp->token->str);
  gts_file_next_token (fp);

  if (fp->type == GTS_INT || fp->type == GTS_FLOAT) {
    p->force.x = atof (fp->token->str);
    gts_file_next_token (fp);
  }
  
  if (fp->type == GTS_INT || fp->type == GTS_FLOAT) {
    p->force.y = atof (fp->token->str);
    gts_file_next_token (fp);
  }
  
  if (fp->type == GTS_INT || fp->type == GTS_FLOAT) {
    p->force.z = atof (fp->token->str);
    gts_file_next_token (fp);
  }
}

static void gfs_particulate_write (GtsObject * o, FILE * fp)
{
  (* GTS_OBJECT_CLASS (gfs_particulate_class ())->parent_class->write) (o, fp);
 
 GfsParticulate * p = GFS_PARTICULATE (o);
  fprintf (fp, " %g %g %g %g %g", p->mass, p->volume, p->vel.x, p->vel.y, p->vel.z);
  fprintf (fp, " %g %g %g", p->force.x, p->force.y, p->force.z);
}

static void gfs_particulate_class_init (GfsEventClass * klass)
{
  klass->event = gfs_particulate_event;
  GTS_OBJECT_CLASS (klass)->read = gfs_particulate_read;
  GTS_OBJECT_CLASS (klass)->write = gfs_particulate_write;
}

GfsEventClass * gfs_particulate_class (void)
{
  static GfsEventClass * klass = NULL;

  if (klass == NULL) {
    GtsObjectClassInfo gfs_particulate_info = {
      "GfsParticulate",
      sizeof (GfsParticulate),
      sizeof (GfsEventClass),
      (GtsObjectClassInitFunc) gfs_particulate_class_init,
      (GtsArgSetFunc) NULL,
      (GtsArgGetFunc) NULL
    };
    klass = gts_object_class_new (GTS_OBJECT_CLASS (gfs_particle_class ()),
				  &gfs_particulate_info);
  }
  return klass;
}

/* GfsParticleList: Object */

static void assign_forces(GfsParticulate *particulate, GtsSListContainer *forces)
{
  particulate->forces = forces;
}

static void gfs_particle_list_read (GtsObject ** o, GtsFile * fp)
{
  if (GTS_OBJECT_CLASS (gfs_particle_list_class ())->parent_class->read)
    (* GTS_OBJECT_CLASS (gfs_particle_list_class ())->parent_class->read) 
      (o, fp);
  if (fp->type == GTS_ERROR)
    return;

  GfsParticleList * p = GFS_PARTICLE_LIST (*o);  
  GfsEventList * l = GFS_EVENT_LIST (p);  
  if (fp->type == '{') {
    fp->scope_max++;
    gts_file_next_token (fp);
    
    while (fp->type == '\n')
      gts_file_next_token (fp);
  
    GfsSimulation * sim = gfs_object_simulation (*o);
    GtsObjectClass * klass;
    while (fp->type != '}') {      

      if (fp->type != GTS_STRING) {
	gts_file_error (fp, "expecting a keyword (GfsParticleForce)");
	break;
      }
      klass = gfs_object_class_from_name (fp->token->str);
 
      if (klass == NULL) {
	gts_file_error (fp, "unknown class `%s'", fp->token->str);
	break;
      }
      if (!gts_object_class_is_from_class (klass, gfs_particle_force_class ())) {
	gts_file_error (fp, "'%s' is not a GfsParticleForce", fp->token->str);
	break;
      }
  
      GtsObject * object = gts_object_new (klass);
      gfs_object_simulation_set (object, sim);
  
      (* klass->read) (&object, fp);
    
      if (fp->type == GTS_ERROR) {
	gts_object_destroy (object);
	break;
      }
  
      while (fp->type == '\n') 
	gts_file_next_token (fp);
      
      gts_container_add (GTS_CONTAINER (p->forces), GTS_CONTAINEE (object));   
    }
    
    if (fp->type != '}') {
      gts_file_error (fp, "expecting a closing brace");
      return;
    }
    fp->scope_max--;
    gts_file_next_token (fp);
 
  }

  p->forces->items = g_slist_reverse (p->forces->items);
 
  gts_container_foreach (GTS_CONTAINER (l->list), (GtsFunc) assign_forces, p->forces);

  if(fp->type == GTS_INT){
    p->idlast = atoi (fp->token->str);
    gts_file_next_token (fp);
  }    
}

static void gfs_particle_list_write (GtsObject * o, FILE * fp)
{
  (* GTS_OBJECT_CLASS (gfs_particle_list_class ())->parent_class->write) (o, fp);

  GfsParticleList * p = GFS_PARTICLE_LIST (o);
  fputs (" {\n", fp);
  GSList * i = p->forces->items;
  while (i) {
    fputs ("    ", fp);
    (* GTS_OBJECT (i->data)->klass->write) (i->data, fp);
    fputc ('\n', fp);
    i = i->next; 
  }
  fputc ('}', fp);

  fprintf (fp, " %d", p->idlast);
}

static void gfs_particle_list_init (GtsObject *o){

  GfsParticleList * plist = GFS_PARTICLE_LIST(o);

  plist->forces = 
    GTS_SLIST_CONTAINER (gts_container_new (GTS_CONTAINER_CLASS (gts_slist_container_class ())));
 
}

static void gfs_particle_list_destroy (GtsObject * o)
{
  GfsParticleList * plist = GFS_PARTICLE_LIST(o);
 
  gts_container_foreach (GTS_CONTAINER (plist->forces), (GtsFunc) gts_object_destroy, NULL);
  gts_object_destroy (GTS_OBJECT (plist->forces));

  (* GTS_OBJECT_CLASS (gfs_particle_list_class ())->parent_class->destroy) (o);
}

static void gfs_particle_list_class_init (GtsObjectClass * klass)
{
  klass->read = gfs_particle_list_read;
  klass->write = gfs_particle_list_write;  
  klass->destroy = gfs_particle_list_destroy;  
}

GfsEventClass * gfs_particle_list_class (void)
{
  static GfsEventClass * klass = NULL;

  if (klass == NULL) {
    GtsObjectClassInfo gfs_particle_list_info = {
      "GfsParticleList",
      sizeof (GfsParticleList),
      sizeof (GfsEventClass),
      (GtsObjectClassInitFunc) gfs_particle_list_class_init,
      (GtsObjectInitFunc) gfs_particle_list_init,
      (GtsArgSetFunc) NULL,
      (GtsArgGetFunc) NULL
    };
    klass = gts_object_class_new (GTS_OBJECT_CLASS (gfs_event_list_class ()),
				  &gfs_particle_list_info);
  }
  return klass;
}

/* Initialize modules */
const gchar * g_module_check_init (void);

const gchar * g_module_check_init (void)
{ 
  gfs_particulate_class ();
  gfs_particle_list_class ();
  gfs_force_lift_class ();
  gfs_force_drag_class ();
  gfs_force_buoy_class ();
  gfs_particle_force_class ();
  return NULL; 
}