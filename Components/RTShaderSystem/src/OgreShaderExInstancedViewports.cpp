/*
-----------------------------------------------------------------------------
This source file is part of OGRE
(Object-oriented Graphics Rendering Engine)
For the latest info, see http://www.ogre3d.org

Copyright (c) 2000-2009 Torus Knot Software Ltd
Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
-----------------------------------------------------------------------------
*/
#include "OgreShaderExInstancedViewports.h"
#ifdef RTSHADER_SYSTEM_BUILD_EXT_SHADERS
#include "OgreShaderFFPRenderState.h"
#include "OgreShaderProgram.h"
#include "OgreShaderParameter.h"
#include "OgreShaderProgramSet.h"
#include "OgreScriptCompiler.h"

namespace Ogre {
namespace RTShader {


/************************************************************************/
/*                                                                      */
/************************************************************************/
String ShaderExInstancedViewports::Type							= "SGX_InstancedViewports";

//-----------------------------------------------------------------------
#define SGX_LIB_INSTANCED_VIEWPORTS					        "SampleLib_InstancedViewports"
#define SGX_FUNC_INSTANCED_VIEWPORTS_TRANSFORM			    "SGX_InstancedViewportsTransform"
#define SGX_FUNC_INSTANCED_VIEWPORTS_DISCARD_OUT_OF_BOUNDS	"SGX_InstancedViewportsDiscardOutOfBounds"


//-----------------------------------------------------------------------
ShaderExInstancedViewports::ShaderExInstancedViewports()
{
	mMonitorsCount				= Vector2(1.0, 1.0);			
	mMonitorsCountChanged		= true;
}

//-----------------------------------------------------------------------
const String& ShaderExInstancedViewports::getType() const
{
	return Type;
}


//-----------------------------------------------------------------------
int	ShaderExInstancedViewports::getExecutionOrder() const
{
	// We place this effect after texturing stage and before fog stage.
	return FFP_POST_PROCESS+1;
}

//-----------------------------------------------------------------------
void ShaderExInstancedViewports::copyFrom(const SubRenderState& rhs)
{
	const ShaderExInstancedViewports& rhsInstancedViewports = static_cast<const ShaderExInstancedViewports&>(rhs);
	
	// Copy all settings that affect this sub render state output code.
	mMonitorsCount = rhsInstancedViewports.mMonitorsCount;
	mMonitorsCountChanged = rhsInstancedViewports.mMonitorsCountChanged;
}

//-----------------------------------------------------------------------
bool ShaderExInstancedViewports::preAddToRenderState( const RenderState* renderState, Pass* srcPass, Pass* dstPass )
{
	return srcPass->getParent()->getParent()->getName().find("SdkTrays") == Ogre::String::npos;
}
//-----------------------------------------------------------------------
bool ShaderExInstancedViewports::resolveParameters(ProgramSet* programSet)
{
	Program* vsProgram = programSet->getCpuVertexProgram();
	Program* psProgram = programSet->getCpuFragmentProgram();
	Function* vsMain = vsProgram->getEntryPointFunction();
	Function* psMain = psProgram->getEntryPointFunction();


	// Resolve vertex shader output position in projective space.

	mVSInPosition = vsMain->resolveInputParameter(Parameter::SPS_POSITION, 0, Parameter::SPC_POSITION_OBJECT_SPACE, GCT_FLOAT4);
	if (mVSInPosition.get() == NULL)
		return false;

	mVSOriginalOutPositionProjectiveSpace = vsMain->resolveOutputParameter(Parameter::SPS_POSITION, 0, Parameter::SPC_POSITION_PROJECTIVE_SPACE, GCT_FLOAT4);
	if (mVSOriginalOutPositionProjectiveSpace.get() == NULL)
		return false;

#define SPC_POSITION_PROJECTIVE_SPACE_AS_TEXCORD ((Parameter::Content)(Parameter::SPC_CUSTOM_CONTENT_BEGIN + 1))

	mVSOutPositionProjectiveSpace = vsMain->resolveOutputParameter(Parameter::SPS_TEXTURE_COORDINATES, -1, SPC_POSITION_PROJECTIVE_SPACE_AS_TEXCORD, GCT_FLOAT4);
	if (mVSOutPositionProjectiveSpace.get() == NULL)
		return false;

	// Resolve ps input position in projective space.
	mPSInPositionProjectiveSpace = psMain->resolveInputParameter(Parameter::SPS_TEXTURE_COORDINATES, 
		mVSOutPositionProjectiveSpace->getIndex(), 
		mVSOutPositionProjectiveSpace->getContent(),
		GCT_FLOAT4);
	if (mPSInPositionProjectiveSpace.get() == NULL)
		return false;

	// Resolve vertex shader uniform monitors count
	mVSInMonitorsCount = vsProgram->resolveParameter(GCT_FLOAT2, -1, (uint16)GPV_GLOBAL, "monitorsCount");
	if (mVSInMonitorsCount.get() == NULL)
		return false;

	// Resolve pixel shader uniform monitors count
	mPSInMonitorsCount = psProgram->resolveParameter(GCT_FLOAT2, -1, (uint16)GPV_GLOBAL, "monitorsCount");
	if (mPSInMonitorsCount.get() == NULL)
		return false;




	// Resolve the current world & view matrices concatenated	
	mWorldViewMatrix = vsProgram->resolveAutoParameterInt(GpuProgramParameters::ACT_WORLDVIEW_MATRIX, 0);
	if (mWorldViewMatrix.get() == NULL)
		return false;	

	// Resolve the current projection matrix
	mProjectionMatrix = vsProgram->resolveAutoParameterInt(GpuProgramParameters::ACT_PROJECTION_MATRIX, 0);
	if (mProjectionMatrix.get() == NULL)
		return false;	

#define SPC_MONITOR_INDEX ((Parameter::Content)(Parameter::SPC_CUSTOM_CONTENT_BEGIN + 2))
	// Resolve vertex shader  monitor index
	mVSInMonitorIndex = vsMain->resolveInputParameter(Parameter::SPS_TEXTURE_COORDINATES, 3, SPC_MONITOR_INDEX, GCT_FLOAT2);
	if (mVSInMonitorIndex.get() == NULL)
		return false;

#define SPC_MATRIX_R0 ((Parameter::Content)(Parameter::SPC_CUSTOM_CONTENT_BEGIN + 3))
#define SPC_MATRIX_R1 ((Parameter::Content)(Parameter::SPC_CUSTOM_CONTENT_BEGIN + 4))
#define SPC_MATRIX_R2 ((Parameter::Content)(Parameter::SPC_CUSTOM_CONTENT_BEGIN + 5))
#define SPC_MATRIX_R3 ((Parameter::Content)(Parameter::SPC_CUSTOM_CONTENT_BEGIN + 6))

	// Resolve vertex shader viewport offset matrix
	mVSInViewportOffsetMatrixR0 = vsMain->resolveInputParameter(Parameter::SPS_TEXTURE_COORDINATES, 4, SPC_MATRIX_R0, GCT_FLOAT4);
	if (mVSInViewportOffsetMatrixR0.get() == NULL)
		return false;
	mVSInViewportOffsetMatrixR1 = vsMain->resolveInputParameter(Parameter::SPS_TEXTURE_COORDINATES, 5, SPC_MATRIX_R1, GCT_FLOAT4);
	if (mVSInViewportOffsetMatrixR1.get() == NULL)
		return false;
	mVSInViewportOffsetMatrixR2 = vsMain->resolveInputParameter(Parameter::SPS_TEXTURE_COORDINATES, 6, SPC_MATRIX_R2, GCT_FLOAT4);
	if (mVSInViewportOffsetMatrixR2.get() == NULL)
		return false;
	mVSInViewportOffsetMatrixR3 = vsMain->resolveInputParameter(Parameter::SPS_TEXTURE_COORDINATES, 7, SPC_MATRIX_R3, GCT_FLOAT4);
	if (mVSInViewportOffsetMatrixR3.get() == NULL)
		return false;


	
    // Resolve vertex shader output monitor index.	
	mVSOutMonitorIndex = vsMain->resolveOutputParameter(Parameter::SPS_TEXTURE_COORDINATES, -1, 
			SPC_MONITOR_INDEX,
			GCT_FLOAT2);
	if (mVSOutMonitorIndex.get() == NULL)
		return false;

	// Resolve ps input monitor index.
	mPSInMonitorIndex = psMain->resolveInputParameter(Parameter::SPS_TEXTURE_COORDINATES, 
		mVSOutMonitorIndex->getIndex(), 
		mVSOutMonitorIndex->getContent(),
		GCT_FLOAT2);
	if (mPSInMonitorIndex.get() == NULL)
		return false;

	return true;
}

//-----------------------------------------------------------------------
bool ShaderExInstancedViewports::resolveDependencies(ProgramSet* programSet)
{
	Program* vsProgram = programSet->getCpuVertexProgram();
	Program* psProgram = programSet->getCpuFragmentProgram();

	vsProgram->addDependency(FFP_LIB_COMMON);
	vsProgram->addDependency(SGX_LIB_INSTANCED_VIEWPORTS);
	
	psProgram->addDependency(FFP_LIB_COMMON);
	psProgram->addDependency(SGX_LIB_INSTANCED_VIEWPORTS);
	
	return true;
}


//-----------------------------------------------------------------------
bool ShaderExInstancedViewports::addFunctionInvocations(ProgramSet* programSet)
{
	Program* vsProgram = programSet->getCpuVertexProgram();	
	Function* vsMain = vsProgram->getEntryPointFunction();	
	Program* psProgram = programSet->getCpuFragmentProgram();
	Function* psMain = psProgram->getEntryPointFunction();	
	

	// Add vertex shader invocations.
	if (false == addVSInvocations(vsMain, FFP_VS_TRANSFORM + 1))
		return false;


	// Add pixel shader invocations.
	if (false == addPSInvocations(psMain, FFP_PS_PRE_PROCESS + 1))
		return false;
	
	return true;
}

//-----------------------------------------------------------------------
bool ShaderExInstancedViewports::addVSInvocations( Function* vsMain, const int groupOrder )
{
	FunctionInvocation* funcInvocation = NULL;
	int internalCounter = 0;
	
    {
        FunctionInvocation* funcInvocation = NULL;
        int internalCounter = 0;

        funcInvocation = OGRE_NEW FunctionInvocation(SGX_FUNC_INSTANCED_VIEWPORTS_TRANSFORM, groupOrder, internalCounter++);
        funcInvocation->pushOperand(mVSInPosition, Operand::OPS_IN);
        funcInvocation->pushOperand(mWorldViewMatrix, Operand::OPS_IN);
        funcInvocation->pushOperand(mProjectionMatrix, Operand::OPS_IN);
        funcInvocation->pushOperand(mVSInViewportOffsetMatrixR0, Operand::OPS_IN);
        funcInvocation->pushOperand(mVSInViewportOffsetMatrixR1, Operand::OPS_IN);
        funcInvocation->pushOperand(mVSInViewportOffsetMatrixR2, Operand::OPS_IN);
        funcInvocation->pushOperand(mVSInViewportOffsetMatrixR3, Operand::OPS_IN);
        funcInvocation->pushOperand(mVSInMonitorsCount, Operand::OPS_IN);
        funcInvocation->pushOperand(mVSInMonitorIndex, Operand::OPS_IN);
        funcInvocation->pushOperand(mVSOriginalOutPositionProjectiveSpace, Operand::OPS_OUT);

        vsMain->addAtomInstance(funcInvocation);
    }

    {
        // Output position in projective space.
        funcInvocation = OGRE_NEW FunctionInvocation(FFP_FUNC_ASSIGN,  groupOrder, internalCounter++); 
        funcInvocation->pushOperand(mVSOriginalOutPositionProjectiveSpace, Operand::OPS_IN);
        funcInvocation->pushOperand(mVSOutPositionProjectiveSpace, Operand::OPS_OUT);
        vsMain->addAtomInstance(funcInvocation);
    }

    {
        // Output monitor index.
        funcInvocation = OGRE_NEW FunctionInvocation(FFP_FUNC_ASSIGN,  groupOrder, internalCounter++); 
        funcInvocation->pushOperand(mVSInMonitorIndex, Operand::OPS_IN);
        funcInvocation->pushOperand(mVSOutMonitorIndex, Operand::OPS_OUT);
        vsMain->addAtomInstance(funcInvocation);
    }


	return true;
}

//-----------------------------------------------------------------------
bool ShaderExInstancedViewports::addPSInvocations( Function* psMain, const int groupOrder )
{
	FunctionInvocation* funcInvocation = NULL;
	int internalCounter = 0;

	funcInvocation = OGRE_NEW FunctionInvocation(SGX_FUNC_INSTANCED_VIEWPORTS_DISCARD_OUT_OF_BOUNDS, groupOrder, internalCounter++);
	funcInvocation->pushOperand(mPSInMonitorsCount, Operand::OPS_IN);
	funcInvocation->pushOperand(mPSInMonitorIndex, Operand::OPS_IN);
	funcInvocation->pushOperand(mPSInPositionProjectiveSpace, Operand::OPS_IN);
	
	psMain->addAtomInstance(funcInvocation);

	return true;
}
//-----------------------------------------------------------------------
void ShaderExInstancedViewports::updateGpuProgramsParams(Renderable* rend, Pass* pass, const AutoParamDataSource* source, const LightList* pLightList)
{
	if (mMonitorsCountChanged)
	{
		mVSInMonitorsCount->setGpuParameter(mMonitorsCount + Vector2(0.0001, 0.0001));
		mPSInMonitorsCount->setGpuParameter(mMonitorsCount + Vector2(0.0001, 0.0001));

		mMonitorsCountChanged = false;
	}	
}
//-----------------------------------------------------------------------
void ShaderExInstancedViewports::setMonitorsCount( const Vector2 monitorsCount )
{
    mMonitorsCount = monitorsCount;
    mMonitorsCountChanged = true;
}
//-----------------------------------------------------------------------
SubRenderState*	ShaderExInstancedViewportsFactory::createInstance(ScriptCompiler* compiler, 
														 PropertyAbstractNode* prop, Pass* pass, SGScriptTranslator* translator)
{
	SubRenderState* subRenderState = SubRenderStateFactory::createInstance();
	ShaderExInstancedViewports* InstancedViewportsSubRenderState = static_cast<ShaderExInstancedViewports*>(subRenderState);
	return subRenderState;								
}
//-----------------------------------------------------------------------
void ShaderExInstancedViewportsFactory::writeInstance(MaterialSerializer* ser, 
											 SubRenderState* subRenderState, 
											 Pass* srcPass, Pass* dstPass)
{

}

//-----------------------------------------------------------------------
const String& ShaderExInstancedViewportsFactory::getType() const
{
	return ShaderExInstancedViewports::Type;
}


//-----------------------------------------------------------------------
SubRenderState*	ShaderExInstancedViewportsFactory::createInstanceImpl()
{
	return OGRE_NEW ShaderExInstancedViewports;
}

//-----------------------------------------------------------------------

}
}

#endif
