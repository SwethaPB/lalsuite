/*  <lalVerbatim file="LALInspiralPhasing1CV">
Author: Sathyaprakash, B. S.
$Id$
</lalVerbatim>  */

/*  <lalLaTeX>

\subsection{Module \texttt{LALInspiralPhasing1.c}}


\subsubsection*{Prototypes}
\vspace{0.1in}
\input{LALInspiralPhasing1CP}
\index{\verb&LALInspiralPhasing1()&}

\subsubsection*{Description}

The function \texttt{LALInspiralPhasing1} calculates the phase $\phi(v)$ of a gravitational wave from an
inspiralling binary system.

It does this using the following equation, which is one of the pair which constitute the gravitational wave
phasing formula,

\begin{equation}
\phi(v) =  \phi_{0} - 2 \int_{v_{0}}^{v} v^{3} \frac{E'(v)}{{\cal F}(v)} \, dv \,\,.
\label{phiofv}
\end{equation}

\texttt{LALInspiralPhasing1} calculates $\phi(v)$, given $\phi_{0}$, $v_{0}$,  $v$, $E^{\prime}(v)$ and
$\mathcal{F}(v)$.


\subsubsection*{Algorithm}


\subsubsection*{Uses}

\texttt{LALDRombergIntegrate}

\subsubsection*{Notes}

\vfill{\footnotesize\input{LALInspiralPhasing1CV}}

</lalLaTeX>  */

#include <math.h>
#include <lal/LALStdlib.h>
#include <lal/LALInspiral.h>
#include <lal/Integrate.h>

NRCSID (INSPIRALPHASINGC, "$Id$"); 

/*  <lalVerbatim file="LALInspiralPhasing1CP"> */
void LALInspiralPhasing1 (LALStatus *status,
	               REAL8 *phiofv,
	               REAL8 v,
	               void *params)
{ /* </lalVerbatim>  */

   void *funcParams;
   DIntegrateIn intinp;
   PhiofVIntegrandIn in2;
   InspiralPhaseIn *in1;
   REAL8 sign;
   REAL8 answer;

   INITSTATUS (status, "LALInspiralPhasing1", INSPIRALPHASINGC);
   ATTATCHSTATUSPTR (status);

   ASSERT (phiofv, status, LALINSPIRALH_ENULL, LALINSPIRALH_MSGENULL);
   ASSERT (params, status, LALINSPIRALH_ENULL, LALINSPIRALH_MSGENULL);
   ASSERT (v > 0., status, LALINSPIRALH_ESIZE, LALINSPIRALH_MSGESIZE);
   ASSERT (v < 1., status, LALINSPIRALH_ESIZE, LALINSPIRALH_MSGESIZE);

   sign = 1.0;

   in1 = (InspiralPhaseIn *) params;

   intinp.function = LALInspiralPhiofVIntegrand;
   intinp.xmin = in1->v0;
   intinp.xmax = v;
   intinp.type = ClosedInterval;

   in2.dEnergy = in1->dEnergy;
   in2.flux = in1->flux;
   in2.coeffs = in1->coeffs;

   funcParams = (void *) &in2;

   if (v==in1->v0) {
      *phiofv = in1->phi0;
      DETATCHSTATUSPTR (status);
      RETURN (status);
   }

   if(in1->v0 > v) {
      intinp.xmin = v;
      intinp.xmax = in1->v0;
      sign = -1.0;
   }

   LALDRombergIntegrate (status->statusPtr, &answer, &intinp, funcParams);
   CHECKSTATUSPTR (status);

   *phiofv = in1->phi0 - 2.0*sign*answer;

   DETATCHSTATUSPTR (status);
   RETURN (status);
}

