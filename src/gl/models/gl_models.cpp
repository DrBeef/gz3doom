// 
//---------------------------------------------------------------------------
//
// Copyright(C) 2005-2016 Christoph Oelckers
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
** gl_models.cpp
**
** OpenGL renderer model handling code
**
**/

#include "gl_load/gl_system.h"
#include "w_wad.h"
#include "g_game.h"
#include "doomstat.h"
#include "g_level.h"
#include "r_state.h"
#include "d_player.h"
#include "g_levellocals.h"
#include "i_time.h"
#include "hwrenderer/textures/hw_material.h"

#include "gl_load/gl_interface.h"
#include "gl/renderer/gl_renderer.h"
#include "gl/scene/gl_drawinfo.h"
#include "gl/models/gl_models.h"
#include "gl/renderer/gl_renderstate.h"
#include "gl/shaders/gl_shader.h"

#include "hwrenderer/utility/hw_cvars.h"
#include <hwrenderer\utility\hw_vrmodes.h>

CVAR(Bool, gl_light_models, true, CVAR_ARCHIVE)
EXTERN_CVAR(Float, gl_weaponOfsY)
EXTERN_CVAR(Float, gl_weaponOfsZ)

VSMatrix FGLModelRenderer::GetViewToWorldMatrix()
{
	VSMatrix objectToWorldMatrix;
	di->VPUniforms.mViewMatrix.inverseMatrix(objectToWorldMatrix);
	return objectToWorldMatrix;
}



void FGLModelRenderer::PrepareRenderHUDModel(AActor* playermo, FSpriteModelFrame* smf, float ofsX, float ofsY, VSMatrix &objectToWorldMatrix)
{
	auto vrmode = VRMode::GetVRMode(true);
	if (vrmode->mEyeCount > 1)
	{
		//TODO Remove gl_RenderState
		gl_RenderState.AlphaFunc(GL_GEQUAL, gl_mask_sprite_threshold);
		// [BB] Render the weapon in worldspace to confirm transforms are all correct
		gl_RenderState.mModelMatrix.loadIdentity();
		// Need to reset the normal matrix too
		di->VPUniforms.mNormalViewMatrix.loadIdentity();

		if (vrmode->GetWeaponTransform(&gl_RenderState.mModelMatrix))
		{
			float scale = 0.01f;
			gl_RenderState.mModelMatrix.scale(scale, scale, scale);
			gl_RenderState.mModelMatrix.translate(0, 5 + gl_weaponOfsZ, 30 + gl_weaponOfsY);
		}
		else
		{
			DVector3 pos = playermo->InterpolatedPosition(r_viewpoint.TicFrac);
			gl_RenderState.mModelMatrix.translate(pos.X, pos.Z + 40, pos.Y);
			gl_RenderState.mModelMatrix.rotate(-playermo->Angles.Yaw.Degrees - 90, 0, 1, 0);
		}


		// Scaling model (y scale for a sprite means height, i.e. z in the world!).
		gl_RenderState.mModelMatrix.scale(smf->xscale, smf->zscale, smf->yscale);

		// Aplying model offsets (model offsets do not depend on model scalings).
		gl_RenderState.mModelMatrix.translate(smf->xoffset / smf->xscale, smf->zoffset / smf->zscale, smf->yoffset / smf->yscale);

		// [BB] Weapon bob, very similar to the normal Doom weapon bob.
		gl_RenderState.mModelMatrix.rotate(ofsX / 4, 0, 1, 0);
		gl_RenderState.mModelMatrix.rotate((ofsY - WEAPONTOP) / -4., 1, 0, 0);

		// [BB] For some reason the jDoom models need to be rotated.
		gl_RenderState.mModelMatrix.rotate(90.f, 0, 1, 0);

		// Applying angleoffset, pitchoffset, rolloffset.
		gl_RenderState.mModelMatrix.rotate(-smf->angleoffset, 0, 1, 0);
		gl_RenderState.mModelMatrix.rotate(smf->pitchoffset, 0, 0, 1);
		gl_RenderState.mModelMatrix.rotate(-smf->rolloffset, 1, 0, 0);
		gl_RenderState.EnableModelMatrix(true);
		gl_RenderState.EnableModelMatrix(false);
		objectToWorldMatrix = gl_RenderState.mModelMatrix;
	}
	else
	{
		FModelRenderer::PrepareRenderHUDModel(playermo, smf, ofsX, ofsY, objectToWorldMatrix);
	}
}

void FGLModelRenderer::BeginDrawModel(AActor *actor, FSpriteModelFrame *smf, const VSMatrix &objectToWorldMatrix, bool mirrored)
{
	glDepthFunc(GL_LEQUAL);
	gl_RenderState.EnableTexture(true);
	// [BB] In case the model should be rendered translucent, do back face culling.
	// This solves a few of the problems caused by the lack of depth sorting.
	// [Nash] Don't do back face culling if explicitly specified in MODELDEF
	// TO-DO: Implement proper depth sorting.
	if (!(actor->RenderStyle == LegacyRenderStyles[STYLE_Normal]) && !(smf->flags & MDL_DONTCULLBACKFACES))
	{
		glEnable(GL_CULL_FACE);
		glFrontFace((mirrored ^ screen->mPortalState->isMirrored()) ? GL_CCW : GL_CW);
	}

	gl_RenderState.mModelMatrix = objectToWorldMatrix;
	gl_RenderState.EnableModelMatrix(true);
}

void FGLModelRenderer::EndDrawModel(AActor *actor, FSpriteModelFrame *smf)
{
	gl_RenderState.EnableModelMatrix(false);

	glDepthFunc(GL_LESS);
	if (!(actor->RenderStyle == LegacyRenderStyles[STYLE_Normal]) && !(smf->flags & MDL_DONTCULLBACKFACES))
		glDisable(GL_CULL_FACE);
}

void FGLModelRenderer::BeginDrawHUDModel(AActor *actor, const VSMatrix &objectToWorldMatrix, bool mirrored)
{
	glDepthFunc(GL_LEQUAL);

	// [BB] In case the model should be rendered translucent, do back face culling.
	// This solves a few of the problems caused by the lack of depth sorting.
	// TO-DO: Implement proper depth sorting.
	if (!(actor->RenderStyle == LegacyRenderStyles[STYLE_Normal]))
	{
		glEnable(GL_CULL_FACE);
		glFrontFace((mirrored ^ screen->mPortalState->isMirrored()) ? GL_CW : GL_CCW);
	}

	gl_RenderState.mModelMatrix = objectToWorldMatrix;
	gl_RenderState.EnableModelMatrix(true);
}

void FGLModelRenderer::EndDrawHUDModel(AActor *actor)
{
	gl_RenderState.EnableModelMatrix(false);

	glDepthFunc(GL_LESS);
	if (!(actor->RenderStyle == LegacyRenderStyles[STYLE_Normal]))
		glDisable(GL_CULL_FACE);
}

IModelVertexBuffer *FGLModelRenderer::CreateVertexBuffer(bool needindex, bool singleframe)
{
	return new FModelVertexBuffer(needindex, singleframe);
}

void FGLModelRenderer::SetVertexBuffer(IModelVertexBuffer *buffer)
{
	gl_RenderState.SetVertexBuffer((FModelVertexBuffer*)buffer);
}

void FGLModelRenderer::ResetVertexBuffer()
{
	GLRenderer->mVBO->Bind(gl_RenderState);
}

void FGLModelRenderer::SetInterpolation(double inter)
{
	gl_RenderState.SetInterpolationFactor((float)inter);
}

void FGLModelRenderer::SetMaterial(FTexture *skin, bool clampNoFilter, int translation)
{
	FMaterial * tex = FMaterial::ValidateTexture(skin, false);
	gl_RenderState.ApplyMaterial(tex, clampNoFilter ? CLAMP_NOFILTER : CLAMP_NONE, translation, -1);
	/*if (modellightindex != -1)*/ gl_RenderState.SetLightIndex(modellightindex);
	gl_RenderState.Apply();
}

void FGLModelRenderer::DrawArrays(int start, int count)
{
	glDrawArrays(GL_TRIANGLES, start, count);
}

void FGLModelRenderer::DrawElements(int numIndices, size_t offset)
{
	glDrawElements(GL_TRIANGLES, numIndices, GL_UNSIGNED_INT, (void*)(intptr_t)offset);
}

//===========================================================================
//
// Uses a hardware buffer if either single frame (i.e. no interpolation needed)
// or shading is available (interpolation is done by the vertex shader)
//
// If interpolation has to be done on the CPU side this will fall back
// to CPU-side arrays.
//
//===========================================================================

FModelVertexBuffer::FModelVertexBuffer(bool needindex, bool singleframe)
	: FVertexBuffer(true)
{
	vbo_ptr = nullptr;
	ibo_id = 0;
	if (needindex)
	{
		glGenBuffers(1, &ibo_id);	// The index buffer can always be a real buffer.
	}
}

//===========================================================================
//
//
//
//===========================================================================

void FModelVertexBuffer::BindVBO()
{
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo_id);
	glBindBuffer(GL_ARRAY_BUFFER, vbo_id);
	glEnableVertexAttribArray(VATTR_VERTEX);
	glEnableVertexAttribArray(VATTR_TEXCOORD);
	glEnableVertexAttribArray(VATTR_VERTEX2);
	glEnableVertexAttribArray(VATTR_NORMAL);
	glDisableVertexAttribArray(VATTR_COLOR);
}

//===========================================================================
//
//
//
//===========================================================================

FModelVertexBuffer::~FModelVertexBuffer()
{
	if (ibo_id != 0)
	{
		glDeleteBuffers(1, &ibo_id);
	}
	if (vbo_ptr != nullptr)
	{
		delete[] vbo_ptr;
	}
}

//===========================================================================
//
//
//
//===========================================================================

FModelVertex *FModelVertexBuffer::LockVertexBuffer(unsigned int size)
{
	if (vbo_id > 0)
	{
		glBindBuffer(GL_ARRAY_BUFFER, vbo_id);
		glBufferData(GL_ARRAY_BUFFER, size * sizeof(FModelVertex), nullptr, GL_STATIC_DRAW);
		return (FModelVertex*)glMapBufferRange(GL_ARRAY_BUFFER, 0, size * sizeof(FModelVertex), GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT);
	}
	else
	{
		if (vbo_ptr != nullptr) delete[] vbo_ptr;
		vbo_ptr = new FModelVertex[size];
		memset(vbo_ptr, 0, size * sizeof(FModelVertex));
		return vbo_ptr;
	}
}

//===========================================================================
//
//
//
//===========================================================================

void FModelVertexBuffer::UnlockVertexBuffer()
{
	if (vbo_id > 0)
	{
		glBindBuffer(GL_ARRAY_BUFFER, vbo_id);
		glUnmapBuffer(GL_ARRAY_BUFFER);
	}
}

//===========================================================================
//
//
//
//===========================================================================

unsigned int *FModelVertexBuffer::LockIndexBuffer(unsigned int size)
{
	if (ibo_id != 0)
	{
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo_id);
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, size * sizeof(unsigned int), NULL, GL_STATIC_DRAW);
		return (unsigned int*)glMapBufferRange(GL_ELEMENT_ARRAY_BUFFER, 0, size * sizeof(unsigned int), GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT);
	}
	else
	{
		return nullptr;
	}
}

//===========================================================================
//
//
//
//===========================================================================

void FModelVertexBuffer::UnlockIndexBuffer()
{
	if (ibo_id > 0)
	{
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo_id);
		glUnmapBuffer(GL_ELEMENT_ARRAY_BUFFER);
	}
}


//===========================================================================
//
// Sets up the buffer starts for frame interpolation
// This must be called after gl_RenderState.Apply!
//
//===========================================================================
static TArray<FModelVertex> iBuffer;

void FModelVertexBuffer::SetupFrame(FModelRenderer *renderer, unsigned int frame1, unsigned int frame2, unsigned int size)
{
	glBindBuffer(GL_ARRAY_BUFFER, vbo_id);
	glVertexAttribPointer(VATTR_VERTEX, 3, GL_FLOAT, false, sizeof(FModelVertex), &VMO[frame1].x);
	glVertexAttribPointer(VATTR_TEXCOORD, 2, GL_FLOAT, false, sizeof(FModelVertex), &VMO[frame1].u);
	glVertexAttribPointer(VATTR_VERTEX2, 3, GL_FLOAT, false, sizeof(FModelVertex), &VMO[frame2].x);
	glVertexAttribPointer(VATTR_NORMAL, 4, GL_INT_2_10_10_10_REV, true, sizeof(FModelVertex), &VMO[frame2].packedNormal);
}
