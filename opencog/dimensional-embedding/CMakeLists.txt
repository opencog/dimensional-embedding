ADD_LIBRARY (dimensional-embedding SHARED
	DimEmbedModule
	CoverTreePoint
)

INSTALL (TARGETS dimensional-embedding
	LIBRARY DESTINATION "lib${LIB_DIR_SUFFIX}/opencog"
)

TARGET_LINK_LIBRARIES (dimensional-embedding
	${SERVER_LIBRARY}
	${ATOMSPACE_LIBRARIES}
	${COGUTIL_LIBRARY}
)
