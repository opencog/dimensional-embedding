ADD_LIBRARY (dimensionalembedding SHARED
	DimEmbedModule
	CoverTreePoint
)

INSTALL (TARGETS dimensionalembedding
	LIBRARY DESTINATION "lib${LIB_DIR_SUFFIX}/opencog"
)

TARGET_LINK_LIBRARIES (dimensionalembedding
	server
	${ATOMSPACE_LIBRARIES}
	${COGUTIL_LIBRARY}
)
