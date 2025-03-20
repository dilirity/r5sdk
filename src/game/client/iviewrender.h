#ifndef IVIEWRENDER_H
#define IVIEWRENDER_H

class VMatrix;

enum ViewRenderMatrixType_e : unsigned char
{
	VMATRIX_TYPE_VIEW,
	VMATRIX_TYPE_PROJECTION,

	// !NOT A VALID TYPE! - represents the total number of matrix types.
	VMATRIX_TYPE_COUNT
};

abstract_class IViewRender
{
public:
	// SETUP
	// Initialize view renderer
	virtual void		Init(void) = 0;

	// Clear any systems between levels
	virtual void		LevelInit(void) = 0;
	virtual void		LevelShutdown(void) = 0;

	// Shutdown
	virtual void		Shutdown(void) = 0;

	// A set of unknowns here
	virtual void		SubUnk0(void) = 0;
	virtual void		SubUnk1(void) = 0;
	virtual void		SubUnk2(void) = 0;
	virtual void		SubUnk3(void) = 0;
	virtual void		SubUnk4(void) = 0;
	virtual void		SubUnk5(void) = 0;
	virtual void		SubUnk6(void) = 0;
	virtual void		SubUnk7(void) = 0;
	virtual void		SubUnk8(void) = 0;
	virtual void		SubUnk9(void) = 0;
	virtual void		SubUnk10(void) = 0;
	virtual void		SubUnk11(void) = 0;

	// Retrieve details about the world matrices
	virtual VMatrix*	GetViewProjectionMatrix(const ViewRenderMatrixType_e type) = 0;
	virtual VMatrix*	IsViewProjectionMatrixAvailable(const ViewRenderMatrixType_e type) = 0;

	// TODO: reverse engineer the rest.
};

#endif // IVIEWRENDER_H
