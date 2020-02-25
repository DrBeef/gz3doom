#ifndef __GL_DRAWINFO_H
#define __GL_DRAWINFO_H

#include "hwrenderer/scene/hw_drawinfo.h"
#include "hwrenderer/scene/hw_weapon.h"
#include "hwrenderer/scene/hw_viewpointuniforms.h"

#include "gl/renderer/gl_renderstate.h"	// temporary

#ifdef _MSC_VER
#pragma warning(disable:4244)
#endif

struct FDrawInfo : public HWDrawInfo
{
	void RenderPortal(HWPortal *p, bool stencil) override;

    void CreateScene();
    void RenderScene(int recursion);
    void RenderTranslucent();
    void DrawScene(int drawmode, sector_t* sector = nullptr) override;
    void ProcessScene(bool toscreen = false,  sector_t* sector = nullptr);
    void EndDrawScene(sector_t * viewsector);
    void DrawEndScene2D(sector_t * viewsector);
};
#endif
