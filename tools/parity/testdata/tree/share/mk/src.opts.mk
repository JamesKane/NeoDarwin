.if ${MK_FOO} == "no"
MK_BAZ:= no
.endif
.for v in QUX
__DEFAULT_DEPENDENT_OPTIONS+= ${v}_SUPPORT/FOO
.endfor
.include <bsd.mkopt.mk>
