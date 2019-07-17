
LINK_DIRECTORIES ("/usr/local/lib/opencog")

ADD_CXXTEST(DimEmbedUTest)
TARGET_LINK_LIBRARIES(DimEmbedUTest
	dimensional-embedding
	# ${SERVER_LIBRARY}
	# ${ATTENTIONBANK_LIBRARY}
	server
	attentionbonk
	attentionval
	${ATOMSPACE_LIBRARY}
	${COGUTIL_LIBRARY}
	${GUILE_LIBRARIES}
	${Boost_SYSTEM_LIBRARY}
)
