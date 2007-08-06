#define __SP_3DBOX_C__

/*
 * SVG <box3d> implementation
 *
 * Authors:
 *   Lauris Kaplinski <lauris@kaplinski.com>
 *   bulia byak <buliabyak@users.sf.net>
 *   Maximilian Albert <Anhalter42@gmx.de>
 *
 * Copyright (C) 2007      Authors
 * Copyright (C) 1999-2002 Lauris Kaplinski
 * Copyright (C) 2000-2001 Ximian, Inc.
 *
 * Released under GNU GPL, read the file 'COPYING' for more information
 */

#include <glibmm/i18n.h>
#include "attributes.h"
#include "svg/stringstream.h"
#include "box3d.h"

static void sp_3dbox_class_init(SP3DBoxClass *klass);
static void sp_3dbox_init(SP3DBox *box3d);

static void sp_3dbox_build(SPObject *object, SPDocument *document, Inkscape::XML::Node *repr);
static void sp_3dbox_release (SPObject *object);
static void sp_3dbox_set(SPObject *object, unsigned int key, const gchar *value);
static void sp_3dbox_update(SPObject *object, SPCtx *ctx, guint flags);
static Inkscape::XML::Node *sp_3dbox_write(SPObject *object, Inkscape::XML::Node *repr, guint flags);

static gchar *sp_3dbox_description(SPItem *item);

//static void sp_3dbox_set_shape(SPShape *shape);
//static void sp_3dbox_set_shape(SP3DBox *box3d);

static void sp_3dbox_update_corner_with_value_from_svg (SPObject *object, guint corner_id, const gchar *value);
static void sp_3dbox_update_perspective (Box3D::Perspective3D *persp, const gchar *value);
static gchar * sp_3dbox_get_corner_coords_string (SP3DBox *box, guint id);
static std::pair<gdouble, gdouble> sp_3dbox_get_coord_pair_from_string (const gchar *);
static gchar * sp_3dbox_get_perspective_string (SP3DBox *box);

static SPGroupClass *parent_class;

GType
sp_3dbox_get_type(void)
{
    static GType type = 0;

    if (!type) {
        GTypeInfo info = {
            sizeof(SP3DBoxClass),
            NULL,   /* base_init */
            NULL,   /* base_finalize */
            (GClassInitFunc) sp_3dbox_class_init,
            NULL,   /* class_finalize */
            NULL,   /* class_data */
            sizeof(SP3DBox),
            16,     /* n_preallocs */
            (GInstanceInitFunc) sp_3dbox_init,
            NULL,   /* value_table */
        };
        type = g_type_register_static(SP_TYPE_GROUP, "SP3DBox", &info, (GTypeFlags) 0);
    }

    return type;
}

static void
sp_3dbox_class_init(SP3DBoxClass *klass)
{
    SPObjectClass *sp_object_class = (SPObjectClass *) klass;
    SPItemClass *item_class = (SPItemClass *) klass;

    parent_class = (SPGroupClass *) g_type_class_ref(SP_TYPE_GROUP);

    sp_object_class->build = sp_3dbox_build;
    sp_object_class->set = sp_3dbox_set;
    sp_object_class->write = sp_3dbox_write;
    sp_object_class->update = sp_3dbox_update;
    sp_object_class->release = sp_3dbox_release;

    item_class->description = sp_3dbox_description;
}

static void
sp_3dbox_init(SP3DBox *box)
{
    for (int i = 0; i < 8; ++i) box->corners[i] = NR::Point(0,0);
    for (int i = 0; i < 6; ++i) box->faces[i] = NULL;
}

static void
sp_3dbox_build(SPObject *object, SPDocument *document, Inkscape::XML::Node *repr)
{
    if (((SPObjectClass *) (parent_class))->build) {
        ((SPObjectClass *) (parent_class))->build(object, document, repr);
    }

    SP3DBox *box = SP_3DBOX (object);

    if (repr->attribute ("inkscape:perspective") == NULL) {
        // we are creating a new box; link it to the current perspective
        Box3D::Perspective3D::current_perspective->add_box (box);
    } else {
        // create a new perspective that we can compare with existing ones
        Box3D::Perspective3D *persp = new Box3D::Perspective3D (Box3D::VanishingPoint (0,0),
                                                                Box3D::VanishingPoint (0,0),
                                                                Box3D::VanishingPoint (0,0));
        sp_3dbox_update_perspective (persp, repr->attribute ("inkscape:perspective"));
        SPDesktop *desktop = inkscape_active_desktop();
        Box3D::Perspective3D *comp = desktop->find_perspective (persp);
        if (comp == NULL) {
            // perspective doesn't exist yet
            desktop->add_perspective (persp);
            persp->add_box (box);
        } else {
            // link the box to the existing perspective and delete the temporary one
            comp->add_box (box);
            delete persp;
            //g_assert (Box3D::get_persp_of_box (box) == comp);

            // FIXME: If the paths of the box's faces do not correspond to the svg representation of the perspective
            //        the box is shown with a "wrong" initial shape that is only corrected after dragging.
            //        Should we "repair" this by updating the paths at the end of sp_3dbox_build()?
            //        Maybe it would be better to simply destroy and rebuild them in sp_3dbox_link_to_existing_paths().
        }
    }

    sp_object_read_attr(object, "inkscape:box3dcornerA");
    sp_object_read_attr(object, "inkscape:box3dcornerB");
    sp_object_read_attr(object, "inkscape:box3dcornerC");

    // TODO: We create all faces in the beginning, but only the non-degenerate ones
    //       should be written to the svg representation later in sp_3dbox_write.
    Box3D::Axis cur_plane, axis, dir1, dir2;
    Box3D::FrontOrRear cur_pos;
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 2; ++j) {
            cur_plane = Box3D::planes[i];
            cur_pos = Box3D::face_positions[j];
            // FIXME: The following code could theoretically be moved to
            //        the constructor of Box3DFace (but see the comment there).
            axis = (cur_pos == Box3D::FRONT ? Box3D::NONE : Box3D::third_axis_direction (cur_plane));
            dir1 = extract_first_axis_direction (cur_plane);
            dir2 = extract_second_axis_direction (cur_plane);
            
            box->faces[Box3D::face_to_int(cur_plane ^ cur_pos)] =
                new Box3DFace (box, box->corners[axis], box->corners[axis ^ dir1],
                                    box->corners[axis ^ dir1 ^ dir2], box->corners[axis ^ dir2],
                                    cur_plane, cur_pos);
        }
    }

    // Check whether the paths of the faces of the box need to be linked to existing paths in the
    // document (e.g., after a 'redo' operation or after opening a file) and do so if necessary.
    sp_3dbox_link_to_existing_paths (box, repr);

    sp_3dbox_set_ratios (box);
}

static void
sp_3dbox_release (SPObject *object)
{
	SP3DBox *box = SP_3DBOX(object);
        for (int i = 0; i < 6; ++i) {
            if (box->faces[i]) {
                delete box->faces[i]; // FIXME: Anything else to do? Do we need to clean up the face first?
            }
        }

        // FIXME: We do not duplicate perspectives if they are the same for several boxes.
        //        Thus, don't delete the perspective when deleting a box but rather unlink the box from it.
        Box3D::get_persp_of_box (box)->remove_box (box);

	if (((SPObjectClass *) parent_class)->release) {
	  ((SPObjectClass *) parent_class)->release (object);
	}
}

static void sp_3dbox_set(SPObject *object, unsigned int key, const gchar *value)
{
    switch (key) {
        case SP_ATTR_INKSCAPE_3DBOX_CORNER_A:
            sp_3dbox_update_corner_with_value_from_svg (object, 2, value);
            break;
        case SP_ATTR_INKSCAPE_3DBOX_CORNER_B:
            sp_3dbox_update_corner_with_value_from_svg (object, 1, value);
            break;
        case SP_ATTR_INKSCAPE_3DBOX_CORNER_C:
            sp_3dbox_update_corner_with_value_from_svg (object, 5, value);
            break;
        case SP_ATTR_INKSCAPE_3DBOX_PERSPECTIVE:
            sp_3dbox_update_perspective (Box3D::get_persp_of_box (SP_3DBOX (object)), value);
            break;
	default:
            if (((SPObjectClass *) (parent_class))->set) {
                ((SPObjectClass *) (parent_class))->set(object, key, value);
            }
            break;
    }
}

static void
sp_3dbox_update(SPObject *object, SPCtx *ctx, guint flags)
{
    if (flags & (SP_OBJECT_MODIFIED_FLAG | SP_OBJECT_STYLE_MODIFIED_FLAG | SP_OBJECT_VIEWPORT_MODIFIED_FLAG)) {
        SP3DBox *box = SP_3DBOX(object);
        sp_3dbox_link_to_existing_paths (box, SP_OBJECT_REPR(object));
    }

    /* Invoke parent method */
    if (((SPObjectClass *) (parent_class))->update)
        ((SPObjectClass *) (parent_class))->update(object, ctx, flags);
}



static Inkscape::XML::Node *sp_3dbox_write(SPObject *object, Inkscape::XML::Node *repr, guint flags)
{
    SP3DBox *box = SP_3DBOX(object);
    // FIXME: How to handle other contexts???
    // FIXME: Is tools_isactive(..) more recommended to check for the current context/tool?
    if (!SP_IS_3DBOX_CONTEXT(inkscape_active_event_context()))
        return repr;

    if ((flags & SP_OBJECT_WRITE_BUILD) && !repr) {
        Inkscape::XML::Document *xml_doc = sp_document_repr_doc(SP_OBJECT_DOCUMENT(object));
        repr = xml_doc->createElement("svg:g");
        repr->setAttribute("sodipodi:type", "inkscape:3dbox");
        /* Hook paths to the faces of the box */
        for (int i = 0; i < 6; ++i) {
            box->faces[i]->hook_path_to_3dbox();
        }
    }

    for (int i = 0; i < 6; ++i) {
        box->faces[i]->set_path_repr();
    }

    if (flags & SP_OBJECT_WRITE_EXT) {
        gchar *str;
        str = sp_3dbox_get_corner_coords_string (box, 2);
        repr->setAttribute("inkscape:box3dcornerA", str);

        str = sp_3dbox_get_corner_coords_string (box, 1);
        repr->setAttribute("inkscape:box3dcornerB", str);

        str = sp_3dbox_get_corner_coords_string (box, 5);
        repr->setAttribute("inkscape:box3dcornerC", str);

        str = sp_3dbox_get_perspective_string (box);
        repr->setAttribute("inkscape:perspective", str);
        sp_3dbox_set_ratios (box);

        g_free ((void *) str);
    }

    if (((SPObjectClass *) (parent_class))->write) {
        ((SPObjectClass *) (parent_class))->write(object, repr, flags);
    }

    return repr;
}

static gchar *
sp_3dbox_description(SPItem *item)
{
    g_return_val_if_fail(SP_IS_3DBOX(item), NULL);

    return g_strdup(_("<b>3D Box</b>"));
}

void sp_3dbox_set_ratios (SP3DBox *box, Box3D::Axis axes)
{
    Box3D::Perspective3D *persp = Box3D::get_persp_of_box (box);
    NR::Point pt;

    if (axes & Box3D::X) {
        pt = persp->get_vanishing_point (Box3D::X)->get_pos();
        box->ratio_x = NR::L2 (pt - box->corners[2]) / NR::L2 (pt - box->corners[3]);
    }

    if (axes & Box3D::Y) {
        pt = persp->get_vanishing_point (Box3D::Y)->get_pos();
        box->ratio_y = NR::L2 (pt - box->corners[2]) / NR::L2 (pt - box->corners[0]);
    }

    if (axes & Box3D::Z) {
        pt = persp->get_vanishing_point (Box3D::Z)->get_pos();
        box->ratio_z = NR::L2 (pt - box->corners[4]) / NR::L2 (pt - box->corners[0]);
    }
}

void
sp_3dbox_position_set (SP3DBoxContext &bc)
{
    SP3DBox *box3d = SP_3DBOX(bc.item);

    sp_3dbox_set_shape(box3d);

    // FIXME: Why does the following call not automatically update the children
    //        of box3d (which is an SPGroup, which should do this)?
    //SP_OBJECT(box3d)->requestDisplayUpdate(SP_OBJECT_MODIFIED_FLAG);

    /**
    SP_OBJECT(box3d->path_face1)->requestDisplayUpdate(SP_OBJECT_MODIFIED_FLAG);
    SP_OBJECT(box3d->path_face2)->requestDisplayUpdate(SP_OBJECT_MODIFIED_FLAG);
    SP_OBJECT(box3d->path_face3)->requestDisplayUpdate(SP_OBJECT_MODIFIED_FLAG);
    SP_OBJECT(box3d->path_face4)->requestDisplayUpdate(SP_OBJECT_MODIFIED_FLAG);
    SP_OBJECT(box3d->path_face5)->requestDisplayUpdate(SP_OBJECT_MODIFIED_FLAG);
    SP_OBJECT(box3d->path_face6)->requestDisplayUpdate(SP_OBJECT_MODIFIED_FLAG);
    ***/
}

//static
void
// FIXME: Note that this is _not_ the virtual set_shape() method inherited from SPShape,
//        since SP3DBox is inherited from SPGroup. The following method is "artificially"
//        called from sp_3dbox_update().
//sp_3dbox_set_shape(SPShape *shape)
sp_3dbox_set_shape(SP3DBox *box3d, bool use_previous_corners)
{
    // FIXME: How to handle other contexts???
    // FIXME: Is tools_isactive(..) more recommended to check for the current context/tool?
    if (!SP_IS_3DBOX_CONTEXT(inkscape_active_event_context()))
        return;
    SP3DBoxContext *bc = SP_3DBOX_CONTEXT(inkscape_active_event_context());

    /* Only update the curves during dragging; setting the svg representations 
       is expensive and only done once at the end */
    if (!use_previous_corners) {
        sp_3dbox_recompute_corners (box3d, bc->drag_origin, bc->drag_ptB, bc->drag_ptC);
    } else {
        sp_3dbox_recompute_corners (box3d, box3d->corners[2], box3d->corners[1], box3d->corners[5]);
    }
    if (bc->extruded) {
        box3d->faces[0]->set_corners (box3d->corners[0], box3d->corners[4], box3d->corners[6], box3d->corners[2]);
        box3d->faces[1]->set_corners (box3d->corners[1], box3d->corners[5], box3d->corners[7], box3d->corners[3]);
        box3d->faces[2]->set_corners (box3d->corners[0], box3d->corners[1], box3d->corners[5], box3d->corners[4]);
        box3d->faces[3]->set_corners (box3d->corners[2], box3d->corners[3], box3d->corners[7], box3d->corners[6]);
        box3d->faces[5]->set_corners (box3d->corners[4], box3d->corners[5], box3d->corners[7], box3d->corners[6]);
    }
    box3d->faces[4]->set_corners (box3d->corners[0], box3d->corners[1], box3d->corners[3], box3d->corners[2]);

    sp_3dbox_update_curves (box3d);
}


void sp_3dbox_recompute_corners (SP3DBox *box, NR::Point const A, NR::Point const B, NR::Point const C)
{
    sp_3dbox_move_corner_in_XY_plane (box, 2, A);
    sp_3dbox_move_corner_in_XY_plane (box, 1, B);
    sp_3dbox_move_corner_in_Z_direction (box, 5, C);
}

void
sp_3dbox_update_curves (SP3DBox *box) {
    for (int i = 0; i < 6; ++i) {
        if (box->faces[i]) box->faces[i]->set_curve();
    }
}

/**
 * In some situations (e.g., after cloning boxes, undo & redo, or reading boxes from a file) there are
 * paths already present in the document which correspond to the faces of newly created boxes, but their
 * 'path' members don't link to them yet. The following function corrects this if necessary.
 */
void
sp_3dbox_link_to_existing_paths (SP3DBox *box, Inkscape::XML::Node *repr) {
    // TODO: We should probably destroy the existing paths and recreate them because we don't know
    //       precisely which path corresponds to which face. Does this make a difference?
    //       In sp_3dbox_write we write the correct paths anyway, don't we? But we could get into
    //       trouble at a later stage when we only write single faces for degenerate boxes.

    SPDocument *document = SP_OBJECT_DOCUMENT(box);
    guint face_id = 0;

    for (Inkscape::XML::Node *i = sp_repr_children(repr); i != NULL; i = sp_repr_next(i)) {
        if (face_id > 5) {
            g_warning ("SVG representation of 3D boxes must contain 6 paths or less.\n");
            break;
        }

        SPObject *face_object = document->getObjectByRepr((Inkscape::XML::Node *) i);
        if (!SP_IS_PATH(face_object)) {
            g_warning ("SVG representation of 3D boxes should only contain paths.\n");
            continue;
        }
        box->faces[face_id]->hook_path_to_3dbox(SP_PATH(face_object));
        ++face_id;
    }
    if (face_id < 6) {
        //g_warning ("SVG representation of 3D boxes should contain exactly 6 paths (degenerate boxes are not yet supported).\n");
        // TODO: Check whether it is safe to add the remaining paths to the box and do so in case it is.
        //       (But we also land here for newly created boxes where we shouldn't add any paths because
        //       This is done in sp_3dbox_write later on.
    }
}

void
sp_3dbox_move_corner_in_XY_plane (SP3DBox *box, guint id, NR::Point pt, Box3D::Axis axes)
{
    Box3D::Perspective3D * persp = Box3D::get_persp_of_box (box);

    NR::Point A (box->corners[id ^ Box3D::XY]);
    if (Box3D::is_single_axis_direction (axes)) {
        pt = Box3D::PerspectiveLine (box->corners[id], axes, persp).closest_to(pt);
    }

    /* set the 'front' corners */
    box->corners[id] = pt;

    Box3D::PerspectiveLine pl_one (A, Box3D::Y, persp);
    Box3D::PerspectiveLine pl_two (pt, Box3D::X, persp);
    box->corners[id ^ Box3D::X] = pl_one.meet(pl_two);

    pl_one = Box3D::PerspectiveLine (A, Box3D::X, persp);
    pl_two = Box3D::PerspectiveLine (pt, Box3D::Y, persp);
    box->corners[id ^ Box3D::Y] = pl_one.meet(pl_two);

    /* set the 'rear' corners */
    NR::Point B (box->corners[id ^ Box3D::XYZ]);

    pl_one = Box3D::PerspectiveLine (box->corners[id ^ Box3D::X], Box3D::Z, persp);
    pl_two = Box3D::PerspectiveLine (B, Box3D::Y, persp);
    box->corners[id ^ Box3D::XZ] = pl_one.meet(pl_two);

    pl_one = Box3D::PerspectiveLine (box->corners[id ^ Box3D::XZ], Box3D::X, persp);
    pl_two = Box3D::PerspectiveLine (pt, Box3D::Z, persp);
    box->corners[id ^ Box3D::Z] = pl_one.meet(pl_two);

    pl_one = Box3D::PerspectiveLine (box->corners[id ^ Box3D::Z], Box3D::Y, persp);
    pl_two = Box3D::PerspectiveLine (B, Box3D::X, persp);
    box->corners[id ^ Box3D::YZ] = pl_one.meet(pl_two);
    
}

void
sp_3dbox_move_corner_in_Z_direction (SP3DBox *box, guint id, NR::Point pt, bool constrained)
{
    if (!constrained) sp_3dbox_move_corner_in_XY_plane (box, id, pt, Box3D::XY);

    Box3D::Perspective3D * persp = Box3D::get_persp_of_box (box);

    /* set the four corners of the face containing corners[id] */
    box->corners[id] = Box3D::PerspectiveLine (box->corners[id], Box3D::Z, persp).closest_to(pt);

    Box3D::PerspectiveLine pl_one (box->corners[id], Box3D::X, persp);
    Box3D::PerspectiveLine pl_two (box->corners[id ^ Box3D::XZ], Box3D::Z, persp);
    box->corners[id ^ Box3D::X] = pl_one.meet(pl_two);

    pl_one = Box3D::PerspectiveLine (box->corners[id ^ Box3D::X], Box3D::Y, persp);
    pl_two = Box3D::PerspectiveLine (box->corners[id ^ Box3D::XYZ], Box3D::Z, persp);
    box->corners[id ^ Box3D::XY] = pl_one.meet(pl_two);

    pl_one = Box3D::PerspectiveLine (box->corners[id], Box3D::Y, persp);
    pl_two = Box3D::PerspectiveLine (box->corners[id ^ Box3D::YZ], Box3D::Z, persp);
    box->corners[id ^ Box3D::Y] = pl_one.meet(pl_two);
}

NR::Maybe<NR::Point>
sp_3dbox_get_center (SP3DBox *box)
{
    return sp_3dbox_get_midpoint_between_corners (box, 0, 7);
}

NR::Maybe<NR::Point>
sp_3dbox_get_midpoint_between_corners (SP3DBox *box, guint id_corner1, guint id_corner2)
{
    Box3D::Axis corner_axes = (Box3D::Axis) (id_corner1 ^ id_corner2);

    // Is all this sufficiently precise also for degenerate cases?
    if (sp_3dbox_corners_are_adjacent (id_corner1, id_corner2)) {
        Box3D::Axis orth_dir = get_perpendicular_axis_direction (corner_axes);

        Box3D::Line diag1 (box->corners[id_corner1], box->corners[id_corner2 ^ orth_dir]);
        Box3D::Line diag2 (box->corners[id_corner1 ^ orth_dir], box->corners[id_corner2]);
        NR::Maybe<NR::Point> adjacent_face_center = diag1.intersect(diag2);

        if (!adjacent_face_center) return NR::Nothing();

        Box3D::Perspective3D * persp = Box3D::get_persp_of_box (box);

        Box3D::PerspectiveLine pl (*adjacent_face_center, orth_dir, persp);
        return pl.intersect(Box3D::PerspectiveLine(box->corners[id_corner1], corner_axes, persp));
    } else {
        Box3D::Axis dir = Box3D::extract_first_axis_direction (corner_axes);
        Box3D::Line diag1 (box->corners[id_corner1], box->corners[id_corner2]);
        Box3D::Line diag2 (box->corners[id_corner1 ^ dir], box->corners[id_corner2 ^ dir]);
        return diag1.intersect(diag2);
    }
}

static gchar *
sp_3dbox_get_corner_coords_string (SP3DBox *box, guint id)
{
    id = id % 8;
    Inkscape::SVGOStringStream os;
    os << box->corners[id][NR::X] << "," << box->corners[id][NR::Y];
    return g_strdup(os.str().c_str());
}

static std::pair<gdouble, gdouble>
sp_3dbox_get_coord_pair_from_string (const gchar *coords)
{
    gchar **coordpair = g_strsplit( coords, ",", 0);
    // We might as well rely on g_ascii_strtod to convert the NULL pointer to 0.0,
    // but we include the following test anyway
    if (coordpair[0] == NULL || coordpair[1] == NULL) {
        g_strfreev (coordpair);
        g_warning ("Coordinate conversion failed.\n");
        return std::make_pair(0.0, 0.0);
    }

    gdouble coord1 = g_ascii_strtod(coordpair[0], NULL);
    gdouble coord2 = g_ascii_strtod(coordpair[1], NULL);
    g_strfreev (coordpair);

    return std::make_pair(coord1, coord2);
}

static gchar *
sp_3dbox_get_perspective_string (SP3DBox *box)
{
    
    return sp_3dbox_get_svg_descr_of_persp (Box3D::get_persp_of_box (box));
}
  
gchar *
sp_3dbox_get_svg_descr_of_persp (Box3D::Perspective3D *persp)
{
    // FIXME: We should move this code to perspective3d.cpp, but this yields compiler errors. Why?
    Inkscape::SVGOStringStream os;

    Box3D::VanishingPoint vp = *(persp->get_vanishing_point (Box3D::X));
    os << vp[NR::X] << "," << vp[NR::Y] << ",";
    os << vp.v_dir[NR::X] << "," << vp.v_dir[NR::Y] << ",";
    if (vp.is_finite()) {
        os << "finite,";
    } else {
        os << "infinite,";
    }

    vp = *(persp->get_vanishing_point (Box3D::Y));
    os << vp[NR::X] << "," << vp[NR::Y] << ",";
    os << vp.v_dir[NR::X] << "," << vp.v_dir[NR::Y] << ",";
    if (vp.is_finite()) {
        os << "finite,";
    } else {
        os << "infinite,";
    }

    vp = *(persp->get_vanishing_point (Box3D::Z));
    os << vp[NR::X] << "," << vp[NR::Y] << ",";
    os << vp.v_dir[NR::X] << "," << vp.v_dir[NR::Y] << ",";
    if (vp.is_finite()) {
        os << "finite";
    } else {
        os << "infinite";
    }

    return g_strdup(os.str().c_str());
}

// auxiliary functions
static void
sp_3dbox_update_corner_with_value_from_svg (SPObject *object, guint corner_id, const gchar *value)
{
    if (value == NULL) return;
    SP3DBox *box = SP_3DBOX(object);

    std::pair<gdouble, gdouble> coord_pair = sp_3dbox_get_coord_pair_from_string (value);
    box->corners[corner_id] = NR::Point (coord_pair.first, coord_pair.second);
    sp_3dbox_recompute_corners (box, box->corners[2], box->corners[1], box->corners[5]);
    object->requestDisplayUpdate(SP_OBJECT_MODIFIED_FLAG);
}

static void
sp_3dbox_update_perspective (Box3D::Perspective3D *persp, const gchar *value)
{
    // WARNING! This function changes the perspective associated to 'box'. Since there may be
    // many other boxes linked to the same perspective, their perspective is also changed.
    // If this behaviour is not desired in all cases, we need a different function.
    if (value == NULL) return;

    gchar **vps = g_strsplit( value, ",", 0);
    for (int i = 0; i < 15; ++i) {
        if (vps[i] == NULL) {
            g_warning ("Malformed svg attribute 'perspective'\n");
            return;
        }
    }

    persp->set_vanishing_point (Box3D::X, g_ascii_strtod (vps[0], NULL), g_ascii_strtod (vps[1], NULL),
                                          g_ascii_strtod (vps[2], NULL), g_ascii_strtod (vps[3], NULL),
                                          Box3D::VP_FINITE);
    persp->set_vanishing_point (Box3D::Y, g_ascii_strtod (vps[5], NULL), g_ascii_strtod (vps[6], NULL),
                                          g_ascii_strtod (vps[7], NULL), g_ascii_strtod (vps[8], NULL),
                                          Box3D::VP_FINITE);
    persp->set_vanishing_point (Box3D::Z, g_ascii_strtod (vps[10], NULL), g_ascii_strtod (vps[11], NULL),
                                          g_ascii_strtod (vps[12], NULL), g_ascii_strtod (vps[13], NULL),
                                          Box3D::VP_FINITE);

    // update the other boxes linked to the same perspective
    persp->reshape_boxes (Box3D::XYZ);
}

/*
  Local Variables:
  mode:c++
  c-file-style:"stroustrup"
  c-file-offsets:((innamespace . 0)(inline-open . 0)(case-label . +))
  indent-tabs-mode:nil
  fill-column:99
  End:
*/
// vim: filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4 :
