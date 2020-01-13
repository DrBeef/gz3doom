#pragma once

#include "r_defs.h"
#include "m_fixed.h"
#include "hwrenderer/scene/hw_clipper.h"
#include "gl_portal.h"
#include "gl/renderer/gl_lightdata.h"
#include "gl/renderer/gl_renderer.h"
#include "r_utility.h"
#include "c_cvars.h"

struct HUDSprite;

class GLSceneDrawer
{
	fixed_t viewx, viewy;	// since the nodes are still fixed point, keeping the view position  also fixed point for node traversal is faster.
	
	subsector_t *currentsubsector;	// used by the line processing code.
	sector_t *currentsector;

	TMap<DPSprite*, int> weapondynlightindex;

	void RenderMultipassStuff(FDrawInfo *di);
	
	void UnclipSubsector(subsector_t *sub);
	void AddLine (seg_t *seg, bool portalclip);
	void PolySubsector(subsector_t * sub);
	void RenderPolyBSPNode (void *node);
	void AddPolyobjs(subsector_t *sub);
	void AddLines(subsector_t * sub, sector_t * sector);
	void AddSpecialPortalLines(subsector_t * sub, sector_t * sector, line_t *line);
	void RenderThings(subsector_t * sub, sector_t * sector);
	void DoSubsector(subsector_t * sub);
	void RenderBSPNode(void *node);

	void RenderScene(FDrawInfo *di, int recursion);
	void RenderTranslucent(FDrawInfo *di);

	void CreateScene(FDrawInfo *di);

public:
	GLSceneDrawer()
	{
		GLPortal::drawer = this;
	}

	Clipper clipper;
	int		FixedColormap;

	angle_t FrustumAngle();
	void SetViewMatrix(float vx, float vy, float vz, bool mirror, bool planemirror);
	void SetViewArea();
	void SetupView(float vx, float vy, float vz, DAngle va, bool mirror, bool planemirror);
	void SetViewAngle(DAngle viewangle);
	void SetProjection(VSMatrix matrix);
	void Set3DViewport(bool mainview);
	void Reset3DViewport();
	void SetFixedColormap(player_t *player);
	void DrawScene(FDrawInfo *di, int drawmode, sector_t* sector = nullptr);
	void ProcessScene(FDrawInfo *di, bool toscreen = false, sector_t* sector = nullptr);
	void EndDrawScene(FDrawInfo *di, sector_t * viewsector);
	void DrawEndScene2D(FDrawInfo *di, sector_t * viewsector);

	sector_t *RenderViewpoint(AActor * camera, IntRect * bounds, float fov, float ratio, float fovratio, bool mainview, bool toscreen);
	sector_t *RenderView(player_t *player);
	void WriteSavePic(player_t *player, FileWriter *file, int width, int height);

	void InitClipper(angle_t a1, angle_t a2)
	{
		clipper.Clear();
		clipper.SafeAddClipRangeRealAngles(a1, a2);
	}

	void SetView()
	{
		viewx = FLOAT2FIXED(r_viewpoint.Pos.X);
		viewy = FLOAT2FIXED(r_viewpoint.Pos.Y);
	}

	void SetColor(int light, int rellight, const FColormap &cm, float alpha, bool weapon = false)
	{
		gl_SetColor(light, rellight, FixedColormap != CM_DEFAULT, cm, alpha, weapon);
	}

	bool CheckFog(sector_t *frontsector, sector_t *backsector)
	{
		if (FixedColormap != CM_DEFAULT) return false;
		return hw_CheckFog(frontsector, backsector);
	}

	void SetFog(int lightlevel, int rellight, const FColormap *cmap, bool isadditive)
	{
		gl_SetFog(lightlevel, rellight, FixedColormap != CM_DEFAULT, cmap, isadditive);
	}
};
