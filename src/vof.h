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

#ifndef __VOF_H__
#define __VOF_H__

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include "advection.h"

gdouble gfs_line_area              (FttVector * m, 
				    gdouble alpha, 
				    gdouble c1);
void    gfs_line_center            (FttVector * m, 
				    gdouble alpha, 
				    gdouble a, 
				    FttVector * p);
gdouble gfs_line_alpha             (FttVector * m, 
				    gdouble c);
#if FTT_2D
#  define gfs_plane_volume         gfs_line_area
#  define gfs_plane_alpha          gfs_line_alpha
#else /* 3D */
gdouble gfs_plane_volume           (FttVector * m, 
				    gdouble alpha, 
				    gdouble c1);
gdouble gfs_plane_alpha            (FttVector * m, 
				    gdouble c);
void    gfs_plane_center           (FttVector * m, 
				    gdouble alpha, 
				    gdouble a,
				    FttVector * p);
#endif /* 3D */
gdouble gfs_youngs_gradient        (FttCell * cell, 
				    FttComponent c, 
				    GfsVariable * v);
void    gfs_cell_vof_advection     (FttCell * cell,
				    FttComponent c,
				    GfsAdvectionParams * par);
void    gfs_tracer_vof_advection   (GfsDomain * domain,
				    GfsAdvectionParams * par,
				    GfsVariable * half);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __VOF_H__ */
