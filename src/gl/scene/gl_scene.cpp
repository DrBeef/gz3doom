// 
//---------------------------------------------------------------------------
//
// Copyright(C) 2004-2016 Christoph Oelckers
// All rights reserved.
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with this program.  If not, see http://www.gnu.org/licenses/
//
//--------------------------------------------------------------------------
//
/*
** gl_scene.cpp
** manages the rendering of the player's view
**
*/

#include "gl_load/gl_system.h"
#include "gi.h"
#include "m_png.h"
#include "doomstat.h"
#include "g_level.h"
#include "r_data/r_interpolate.h"
#include "r_utility.h"
#include "d_player.h"
#include "p_effect.h"
#include "sbar.h"
#include "po_man.h"
#include "p_local.h"
#include "serializer.h"
#include "g_levellocals.h"
#include "hwrenderer/dynlights/hw_dynlightdata.h"

#include "gl/dynlights/gl_lightbuffer.h"
#include "gl_load/gl_interface.h"
#include "gl/system/gl_framebuffer.h"
#include "hwrenderer/utility/hw_cvars.h"
#include "gl/renderer/gl_lightdata.h"
#include "gl/renderer/gl_renderstate.h"
#include "gl/renderer/gl_renderbuffers.h"
#include "gl/data/gl_vertexbuffer.h"
#include "hwrenderer/scene/hw_clipper.h"
#include "gl/scene/gl_drawinfo.h"
#include "gl/scene/gl_portal.h"
#include "gl/scene/gl_scenedrawer.h"
#include "gl/stereo3d/gl_stereo3d.h"
#include "hwrenderer/utility/scoped_view_shifter.h"

//==========================================================================
//
// CVARs
//
//==========================================================================
CVAR(Bool, gl_texture, true, 0)
CVAR(Bool, gl_no_skyclear, false, CVAR_ARCHIVE|CVAR_GLOBALCONFIG)
CVAR(Float, gl_mask_threshold, 0.5f,CVAR_ARCHIVE|CVAR_GLOBALCONFIG)
CVAR(Float, gl_mask_sprite_threshold, 0.5f,CVAR_ARCHIVE|CVAR_GLOBALCONFIG)
CVAR(Bool, gl_sort_textures, false, CVAR_ARCHIVE|CVAR_GLOBALCONFIG)

EXTERN_CVAR (Bool, cl_capfps)
EXTERN_CVAR (Bool, r_deathcamera)
EXTERN_CVAR (Float, r_visibility)
EXTERN_CVAR (Bool, r_drawvoxels)

//-----------------------------------------------------------------------------
//
// resets the 3D viewport
//
//-----------------------------------------------------------------------------

void GLSceneDrawer::Reset3DViewport()
{
	glViewport(screen->mScreenViewport.left, screen->mScreenViewport.top, screen->mScreenViewport.width, screen->mScreenViewport.height);
}

//-----------------------------------------------------------------------------
//
// sets 3D viewport and initial state
//
//-----------------------------------------------------------------------------

void GLSceneDrawer::Set3DViewport(bool mainview)
{
	if (mainview && GLRenderer->buffersActive)
	{
		bool useSSAO = (gl_ssao != 0);
		GLRenderer->mBuffers->BindSceneFB(useSSAO);
		gl_RenderState.SetPassType(useSSAO ? GBUFFER_PASS : NORMAL_PASS);
		gl_RenderState.EnableDrawBuffers(gl_RenderState.GetPassDrawBufferCount());
		gl_RenderState.Apply();
	}

	// Always clear all buffers with scissor test disabled.
	// This is faster on newer hardware because it allows the GPU to skip
	// reading from slower memory where the full buffers are stored.
	glDisable(GL_SCISSOR_TEST);
	glClearColor(GLRenderer->mSceneClearColor[0], GLRenderer->mSceneClearColor[1], GLRenderer->mSceneClearColor[2], 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

	const auto &bounds = screen->mSceneViewport;
	glViewport(bounds.left, bounds.top, bounds.width, bounds.height);
	glScissor(bounds.left, bounds.top, bounds.width, bounds.height);

	glEnable(GL_SCISSOR_TEST);

	glEnable(GL_MULTISAMPLE);
	glEnable(GL_DEPTH_TEST);
	glEnable(GL_STENCIL_TEST);
	glStencilFunc(GL_ALWAYS,0,~0);	// default stencil
	glStencilOp(GL_KEEP,GL_KEEP,GL_REPLACE);
}

//-----------------------------------------------------------------------------
//
// SetProjection
// sets projection matrix
//
//-----------------------------------------------------------------------------

void GLSceneDrawer::SetProjection(VSMatrix matrix)
{
	gl_RenderState.mProjectionMatrix.loadIdentity();
	gl_RenderState.mProjectionMatrix.multMatrix(matrix);
}

//-----------------------------------------------------------------------------
//
// Setup the modelview matrix
//
//-----------------------------------------------------------------------------

void GLSceneDrawer::SetViewMatrix(const FRotator &angles, float vx, float vy, float vz, bool mirror, bool planemirror)
{
	float mult = mirror? -1:1;
	float planemult = planemirror? -level.info->pixelstretch : level.info->pixelstretch;

	gl_RenderState.mViewMatrix.loadIdentity();
	gl_RenderState.mViewMatrix.rotate(angles.Roll.Degrees,  0.0f, 0.0f, 1.0f);
	gl_RenderState.mViewMatrix.rotate(angles.Pitch.Degrees, 1.0f, 0.0f, 0.0f);
	gl_RenderState.mViewMatrix.rotate(angles.Yaw.Degrees,   0.0f, mult, 0.0f);
	gl_RenderState.mViewMatrix.translate(vx * mult, -vz * planemult , -vy);
	gl_RenderState.mViewMatrix.scale(-mult, planemult, 1);
}


//-----------------------------------------------------------------------------
//
// SetupView
// Setup the view rotation matrix for the given viewpoint
//
//-----------------------------------------------------------------------------
void GLSceneDrawer::SetupView(FRenderViewpoint &vp, float vx, float vy, float vz, DAngle va, bool mirror, bool planemirror)
{
	vp.SetViewAngle(r_viewwindow);
	SetViewMatrix(vp.HWAngles, vx, vy, vz, mirror, planemirror);
	gl_RenderState.ApplyMatrices();
}

//-----------------------------------------------------------------------------
//
// CreateScene
//
// creates the draw lists for the current scene
//
//-----------------------------------------------------------------------------

void GLSceneDrawer::CreateScene(FDrawInfo *di)
{
	const auto &vp = di->Viewpoint;
	angle_t a1 = di->FrustumAngle();
	di->mClipper->SafeAddClipRangeRealAngles(vp.Angles.Yaw.BAMs() + a1, vp.Angles.Yaw.BAMs() - a1);

	// reset the portal manager
	GLPortal::StartFrame();
	PO_LinkToSubsectors();

	ProcessAll.Clock();

	// clip the scene and fill the drawlists
	for(auto p : level.portalGroups) p->glportal = nullptr;
	Bsp.Clock();
	GLRenderer->mVBO->Map();
	GLRenderer->mLights->Begin();

	// Give the DrawInfo the viewpoint in fixed point because that's what the nodes are.
	di->viewx = FLOAT2FIXED(vp.Pos.X);
	di->viewy = FLOAT2FIXED(vp.Pos.Y);

	validcount++;	// used for processing sidedefs only once by the renderer.
	 
	di->mShadowMap = &GLRenderer->mShadowMap;

	di->RenderBSPNode (level.HeadNode());
	di->PreparePlayerSprites(vp.sector, di->in_area);

	// Process all the sprites on the current portal's back side which touch the portal.
	if (GLRenderer->mCurrentPortal != NULL) GLRenderer->mCurrentPortal->RenderAttached(di);
	Bsp.Unclock();

	// And now the crappy hacks that have to be done to avoid rendering anomalies.
	// These cannot be multithreaded when the time comes because all these depend
	// on the global 'validcount' variable.

	di->HandleMissingTextures(di->in_area);	// Missing upper/lower textures
	di->HandleHackedSubsectors();	// open sector hacks for deep water
	di->ProcessSectorStacks(di->in_area);		// merge visplanes of sector stacks
	GLRenderer->mLights->Finish();
	GLRenderer->mVBO->Unmap();

	ProcessAll.Unclock();

}

//-----------------------------------------------------------------------------
//
// RenderScene
//
// Draws the current draw lists for the non GLSL renderer
//
//-----------------------------------------------------------------------------

void GLSceneDrawer::RenderScene(FDrawInfo *di, int recursion)
{
	const auto &vp = di->Viewpoint;
	RenderAll.Clock();

	glDepthMask(true);
	if (!gl_no_skyclear) GLPortal::RenderFirstSkyPortal(recursion, di);

	gl_RenderState.SetCameraPos(vp.Pos.X, vp.Pos.Y, vp.Pos.Z);

	gl_RenderState.EnableFog(true);
	gl_RenderState.BlendFunc(GL_ONE,GL_ZERO);

	if (gl_sort_textures)
	{
		di->drawlists[GLDL_PLAINWALLS].SortWalls();
		di->drawlists[GLDL_PLAINFLATS].SortFlats();
		di->drawlists[GLDL_MASKEDWALLS].SortWalls();
		di->drawlists[GLDL_MASKEDFLATS].SortFlats();
		di->drawlists[GLDL_MASKEDWALLSOFS].SortWalls();
	}

	// if we don't have a persistently mapped buffer, we have to process all the dynamic lights up front,
	// so that we don't have to do repeated map/unmap calls on the buffer.
	if (gl.lightmethod == LM_DEFERRED && level.HasDynamicLights && !di->isFullbrightScene())
	{
		GLRenderer->mLights->Begin();
		di->drawlists[GLDL_PLAINFLATS].DrawFlats(di, GLPASS_LIGHTSONLY);
		di->drawlists[GLDL_MASKEDFLATS].DrawFlats(di, GLPASS_LIGHTSONLY);
		di->drawlists[GLDL_TRANSLUCENTBORDER].Draw(di, GLPASS_LIGHTSONLY);
		di->drawlists[GLDL_TRANSLUCENT].Draw(di, GLPASS_LIGHTSONLY, true);
		GLRenderer->mLights->Finish();
	}

	// Part 1: solid geometry. This is set up so that there are no transparent parts
	glDepthFunc(GL_LESS);
	gl_RenderState.AlphaFunc(GL_GEQUAL, 0.f);
	glDisable(GL_POLYGON_OFFSET_FILL);

	int pass = GLPASS_ALL;

	gl_RenderState.EnableTexture(gl_texture);
	gl_RenderState.EnableBrightmap(true);
	di->drawlists[GLDL_PLAINWALLS].DrawWalls(di, pass);
	di->drawlists[GLDL_PLAINFLATS].DrawFlats(di, pass);


	// Part 2: masked geometry. This is set up so that only pixels with alpha>gl_mask_threshold will show
	if (!gl_texture) 
	{
		gl_RenderState.EnableTexture(true);
		gl_RenderState.SetTextureMode(TM_MASK);
	}
	gl_RenderState.AlphaFunc(GL_GEQUAL, gl_mask_threshold);
	di->drawlists[GLDL_MASKEDWALLS].DrawWalls(di, pass);
	di->drawlists[GLDL_MASKEDFLATS].DrawFlats(di, pass);

	// Part 3: masked geometry with polygon offset. This list is empty most of the time so only waste time on it when in use.
	if (di->drawlists[GLDL_MASKEDWALLSOFS].Size() > 0)
	{
		glEnable(GL_POLYGON_OFFSET_FILL);
		glPolygonOffset(-1.0f, -128.0f);
		di->drawlists[GLDL_MASKEDWALLSOFS].DrawWalls(di, pass);
		glDisable(GL_POLYGON_OFFSET_FILL);
		glPolygonOffset(0, 0);
	}

	di->drawlists[GLDL_MODELS].Draw(di, pass);

	gl_RenderState.BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	// Part 4: Draw decals (not a real pass)
	glDepthFunc(GL_LEQUAL);
	glEnable(GL_POLYGON_OFFSET_FILL);
	glPolygonOffset(-1.0f, -128.0f);
	glDepthMask(false);
	di->DrawDecals();

	gl_RenderState.SetTextureMode(TM_MODULATE);

	glDepthMask(true);


	// Push bleeding floor/ceiling textures back a little in the z-buffer
	// so they don't interfere with overlapping mid textures.
	glPolygonOffset(1.0f, 128.0f);

	// Part 5: flood all the gaps with the back sector's flat texture
	// This will always be drawn like GLDL_PLAIN, depending on the fog settings
	
	glDepthMask(false);							// don't write to Z-buffer!
	gl_RenderState.EnableFog(true);
	gl_RenderState.AlphaFunc(GL_GEQUAL, 0.f);
	gl_RenderState.BlendFunc(GL_ONE,GL_ZERO);
	di->DrawUnhandledMissingTextures();
	glDepthMask(true);

	glPolygonOffset(0.0f, 0.0f);
	glDisable(GL_POLYGON_OFFSET_FILL);
	RenderAll.Unclock();

}

//-----------------------------------------------------------------------------
//
// RenderTranslucent
//
// Draws the current draw lists for the non GLSL renderer
//
//-----------------------------------------------------------------------------

void GLSceneDrawer::RenderTranslucent(FDrawInfo *di)
{
	const auto &vp = di->Viewpoint;

	RenderAll.Clock();

	gl_RenderState.SetCameraPos(vp.Pos.X, vp.Pos.Y, vp.Pos.Z);

	// final pass: translucent stuff
	gl_RenderState.AlphaFunc(GL_GEQUAL, gl_mask_sprite_threshold);
	gl_RenderState.BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	gl_RenderState.EnableBrightmap(true);
	di->drawlists[GLDL_TRANSLUCENTBORDER].Draw(di, GLPASS_TRANSLUCENT);
	glDepthMask(false);
	di->DrawSorted(GLDL_TRANSLUCENT);
	gl_RenderState.EnableBrightmap(false);


	gl_RenderState.AlphaFunc(GL_GEQUAL, 0.5f);
	glDepthMask(true);

	RenderAll.Unclock();
}


//-----------------------------------------------------------------------------
//
// gl_drawscene - this function renders the scene from the current
// viewpoint, including mirrors and skyboxes and other portals
// It is assumed that the GLPortal::EndFrame returns with the 
// stencil, z-buffer and the projection matrix intact!
//
//-----------------------------------------------------------------------------

void GLSceneDrawer::DrawScene(FDrawInfo *di, int drawmode, sector_t * viewsector)
{
	static int recursion=0;
	static int ssao_portals_available = 0;
	const auto &vp = di->Viewpoint;

	bool applySSAO = false;
	if (drawmode == DM_MAINVIEW)
	{
		ssao_portals_available = gl_ssao_portals;
		applySSAO = true;
	}
	else if (drawmode == DM_OFFSCREEN)
	{
		ssao_portals_available = 0;
	}
	else if (drawmode == DM_PORTAL && ssao_portals_available > 0)
	{
		applySSAO = true;
		ssao_portals_available--;
	}

	if (vp.camera != nullptr)
	{
		ActorRenderFlags savedflags = vp.camera->renderflags;
		CreateScene(di);
		vp.camera->renderflags = savedflags;
	}
	else
	{
		CreateScene(di);
	}

	RenderScene(di, recursion);

	if (s3d::Stereo3DMode::getCurrentMode().RenderPlayerSpritesInScene())
	{
		di->DrawPlayerSprites(IsHUDModelForPlayerAvailable(players[consoleplayer].camera->player));
	}

	if (applySSAO && gl_RenderState.GetPassType() == GBUFFER_PASS)
	{
		gl_RenderState.EnableDrawBuffers(1);
		GLRenderer->AmbientOccludeScene();
		GLRenderer->mBuffers->BindSceneFB(true);
		gl_RenderState.EnableDrawBuffers(gl_RenderState.GetPassDrawBufferCount());
		gl_RenderState.Apply();
		gl_RenderState.ApplyMatrices();
	}

	// Handle all portals after rendering the opaque objects but before
	// doing all translucent stuff
	recursion++;
	GLPortal::EndFrame(di);
	recursion--;
	RenderTranslucent(di);
}

//-----------------------------------------------------------------------------
//
// Draws player sprites and color blend
//
//-----------------------------------------------------------------------------


void GLSceneDrawer::EndDrawScene(FDrawInfo *di, sector_t * viewsector)
{
	gl_RenderState.EnableFog(false);

	// [BB] HUD models need to be rendered here. 
	const bool renderHUDModel = IsHUDModelForPlayerAvailable( players[consoleplayer].camera->player );
	if ( renderHUDModel )
	{
		// [BB] The HUD model should be drawn over everything else already drawn.
		glClear(GL_DEPTH_BUFFER_BIT);
		di->DrawPlayerSprites(true);
	}

	glDisable(GL_STENCIL_TEST);

	Reset3DViewport();

	// Restore standard rendering state
	gl_RenderState.BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	gl_RenderState.ResetColor();
	gl_RenderState.EnableTexture(true);
	glDisable(GL_SCISSOR_TEST);
}

void GLSceneDrawer::DrawEndScene2D(FDrawInfo *di, sector_t * viewsector)
{
	const bool renderHUDModel = IsHUDModelForPlayerAvailable(players[consoleplayer].camera->player);

	// This should be removed once all 2D stuff is really done through the 2D interface.
	gl_RenderState.mViewMatrix.loadIdentity();
	gl_RenderState.mProjectionMatrix.ortho(0, screen->GetWidth(), screen->GetHeight(), 0, -1.0f, 1.0f);
	gl_RenderState.ApplyMatrices();
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_MULTISAMPLE);


	if (!s3d::Stereo3DMode::getCurrentMode().RenderPlayerSpritesInScene())
	{
		// [BB] Only draw the sprites if we didn't render a HUD model before.
		if ( renderHUDModel == false )
		{
			di->DrawPlayerSprites(false);
		}
	}

	gl_RenderState.SetSoftLightLevel(-1);

	// Restore standard rendering state
	gl_RenderState.BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	gl_RenderState.ResetColor();
	gl_RenderState.EnableTexture(true);
	glDisable(GL_SCISSOR_TEST);
}

//-----------------------------------------------------------------------------
//
// R_RenderView - renders one view - either the screen or a camera texture
//
//-----------------------------------------------------------------------------

void GLSceneDrawer::ProcessScene(FDrawInfo *di, bool toscreen, sector_t * viewsector)
{
	iter_dlightf = iter_dlight = draw_dlight = draw_dlightf = 0;
	GLPortal::BeginScene();

	int mapsection = R_PointInSubsector(di->Viewpoint.Pos)->mapsection;
	di->CurrentMapSections.Set(mapsection);
	GLRenderer->mCurrentPortal = nullptr;
	DrawScene(di, toscreen ? DM_MAINVIEW : DM_OFFSCREEN, viewsector);

}

//-----------------------------------------------------------------------------
//
// Renders one viewpoint in a scene
//
//-----------------------------------------------------------------------------

sector_t * GLSceneDrawer::RenderViewpoint (FRenderViewpoint &mainvp, AActor * camera, IntRect * bounds, float fov, float ratio, float fovratio, bool mainview, bool toscreen)
{
	GLRenderer->mSceneClearColor[0] = 0.0f;
	GLRenderer->mSceneClearColor[1] = 0.0f;
	GLRenderer->mSceneClearColor[2] = 0.0f;
	R_SetupFrame (mainvp, r_viewwindow, camera);

	GLRenderer->mGlobVis = R_GetGlobVis(r_viewwindow, r_visibility);

    // Render (potentially) multiple views for stereo 3d
	float viewShift[3];
	const s3d::Stereo3DMode& stereo3dMode = mainview && toscreen? s3d::Stereo3DMode::getCurrentMode() : s3d::Stereo3DMode::getMonoMode();
	stereo3dMode.SetUp();
	for (int eye_ix = 0; eye_ix < stereo3dMode.eye_count(); ++eye_ix)
	{
		const s3d::EyePose * eye = stereo3dMode.getEyePose(eye_ix);
		eye->SetUp();
		screen->SetViewportRects(bounds);
		Set3DViewport(mainview);
		GLRenderer->mDrawingScene2D = true;

		FDrawInfo *di = FDrawInfo::StartDrawInfo(this, mainvp);
		auto& vp = di->Viewpoint;
		di->SetViewArea();
		auto cm =  di->SetFullbrightFlags(mainview ? vp.camera->player : nullptr);
		di->Viewpoint.FieldOfView = fov;	// Set the real FOV for the current scene (it's not necessarily the same as the global setting in r_viewpoint)

		// Stereo mode specific perspective projection
		SetProjection( eye->GetProjection(fov, ratio, fovratio) );
		// SetProjection(fov, ratio, fovratio);	// switch to perspective mode and set up clipper
		vp.SetViewAngle(r_viewwindow);
		// Stereo mode specific viewpoint adjustment - temporarily shifts global ViewPos
		eye->GetViewShift(vp.HWAngles.Yaw.Degrees, viewShift);
		ScopedViewShifter viewShifter(vp.Pos, viewShift);
		SetViewMatrix(vp.HWAngles, vp.Pos.X, vp.Pos.Y, vp.Pos.Z, false, false);
		gl_RenderState.ApplyMatrices();

		ProcessScene(di, toscreen, mainvp.sector);

		if (mainview)
		{
			if (toscreen) EndDrawScene(di, mainvp.sector); // do not call this for camera textures.
			GLRenderer->PostProcessScene(cm, [&]() { DrawEndScene2D(di, mainvp.sector); });

			// This should be done after postprocessing, not before.
			GLRenderer->mBuffers->BindCurrentFB();
			glViewport(screen->mScreenViewport.left, screen->mScreenViewport.top, screen->mScreenViewport.width, screen->mScreenViewport.height);

			if (!toscreen)
			{
				gl_RenderState.mViewMatrix.loadIdentity();
				gl_RenderState.mProjectionMatrix.ortho(screen->mScreenViewport.left, screen->mScreenViewport.width, screen->mScreenViewport.height, screen->mScreenViewport.top, -1.0f, 1.0f);
				gl_RenderState.ApplyMatrices();
			}

			eye->AdjustBlend();
			BlendInfo blendinfo;
			screen->FillBlend(mainvp.sector, blendinfo);
			GLRenderer->DrawBlend(blendinfo);
		}
		di->EndDrawInfo();
		GLRenderer->mDrawingScene2D = false;
		if (!stereo3dMode.IsMono())
			GLRenderer->mBuffers->BlitToEyeTexture(eye_ix);
		eye->TearDown();
	}
	stereo3dMode.TearDown();

	interpolator.RestoreInterpolations ();
	return mainvp.sector;
}

//===========================================================================
//
// Render the view to a savegame picture
//
//===========================================================================

void GLSceneDrawer::WriteSavePic (player_t *player, FileWriter *file, int width, int height)
{
	IntRect bounds;
	bounds.left = 0;
	bounds.top = 0;
	bounds.width = width;
	bounds.height = height;

	// if GLRenderer->mVBO is persistently mapped we must be sure the GPU finished reading from it before we fill it with new data.
	glFinish();

	// Switch to render buffers dimensioned for the savepic
	GLRenderer->mBuffers = GLRenderer->mSaveBuffers;

	P_FindParticleSubsectors();	// make sure that all recently spawned particles have a valid subsector.
	gl_RenderState.SetVertexBuffer(GLRenderer->mVBO);
	GLRenderer->mVBO->Reset();
	GLRenderer->mLights->Clear();

	// This shouldn't overwrite the global viewpoint even for a short time.
	FRenderViewpoint savevp;
	sector_t *viewsector = RenderViewpoint(savevp, players[consoleplayer].camera, &bounds, r_viewpoint.FieldOfView.Degrees, 1.6f, 1.6f, true, false);
	glDisable(GL_STENCIL_TEST);
	gl_RenderState.SetSoftLightLevel(-1);
	GLRenderer->CopyToBackbuffer(&bounds, false);

	// strictly speaking not needed as the glReadPixels should block until the scene is rendered, but this is to safeguard against shitty drivers
	glFinish();

	uint8_t * scr = (uint8_t *)M_Malloc(width * height * 3);
	glReadPixels(0,0,width, height,GL_RGB,GL_UNSIGNED_BYTE,scr);
	M_CreatePNG (file, scr + ((height-1) * width * 3), NULL, SS_RGB, width, height, -width * 3, Gamma);
	M_Free(scr);

	// Switch back the screen render buffers
	screen->SetViewportRects(nullptr);
	GLRenderer->mBuffers = GLRenderer->mScreenBuffers;
}
