UPS = """
<Uintah_specification>
  <Meta>
    <title>%(title)s</title>
  </Meta>
  <SimulationComponent type="phasefield"/>
  <PhaseField type="heat">
    <var>cc</var>
    <dim>3</dim>
    <delt>%(delt)f</delt>
    <alpha>1.</alpha>
    <refine_threshold>0.01</refine_threshold>
    <scheme>crank_nicolson</scheme>
  </PhaseField>
  <Time>
    <maxTime>%(maxt)f</maxTime>
    <initTime>0.</initTime>
    <delt_min>0.</delt_min>
    <delt_max>1.</delt_max>
    <timestep_multiplier>1.</timestep_multiplier>
  </Time>
  <Grid>
    <Level>
      <Box label="0">
        <lower>[  0.,  0.,  0.]</lower>
        <upper>[ 16., 16., 16.]</upper>
        <patches>[%(patches)d,%(patches)d,%(patches)d]</patches>
      </Box>
      <spacing>[%(spacing)f,%(spacing)f,%(spacing)f]</spacing>
    </Level>
    <BoundaryConditions>
      <Face side="x-">
        <BCType id="0" label="u" var="Neumann">
          <value>0.</value>
        </BCType>
      </Face>
      <Face side="y-">
        <BCType id="0" label="u" var="Neumann">
          <value>0.</value>
        </BCType>
      </Face>
      <Face side="z-">
        <BCType id="0" label="u" var="Neumann">
          <value>0.</value>
        </BCType>
      </Face>
      <Face side="x+">
        <BCType id="0" label="u" var="Dirichlet">
          <value>-0.65</value>
        </BCType>
      </Face>
      <Face side="y+">
        <BCType id="0" label="u" var="Dirichlet">
          <value>-0.65</value>
        </BCType>
      </Face>
      <Face side="z+">
        <BCType id="0" label="u" var="Dirichlet">
          <value>-0.65</value>
        </BCType>
      </Face>
    </BoundaryConditions>
  </Grid>
  <AMR>
    <Regridder type="Tiled">
      <adaptive>true</adaptive>
      <max_levels>%(nlvl)d</max_levels>
      <min_patch_size>[[16,16,16]]</min_patch_size>
      <min_boundary_cells>[1,1,1]</min_boundary_cells>
      <cell_refinement_ratio>[[2,2,2]]</cell_refinement_ratio>
      <cell_stability_dilation>[1,1,1]</cell_stability_dilation>
    </Regridder>
    <FineCoarseInterfaces>
      <FCIType id="1" label="u" var="FC1"/>
    </FineCoarseInterfaces>
  </AMR>
  <Solver type="hypre_sstruct" ndim="3" c2f="1">
    <Parameters variable="u">
      <solver>%(solver)s</solver>
      <setupFrequency>0</setupFrequency>
      <updateCoefFrequency>0</updateCoefFrequency>
      <solveFrequency>1</solveFrequency>
    </Parameters>
  </Solver>
  <DataArchiver>
    <filebase>%(name)s.uda</filebase>
    <outputInterval>50</outputInterval>
    <save label="u_norm2_L2"/>
    <save label="u_norm2_H10"/>
    <save label="u_norm2_H20"/>
    <save label="error_norm2_L2"/>
    <save label="error_norm2_H10"/>
    <save label="error_norm2_H20"/>
  </DataArchiver>
</Uintah_specification>"""

NAME = "cn_sstruct_conv_%(solver)s_3d_nlvl%(nlvl)1d_%(ph)d_%(pk)d"

for solver in ("fac","split","gmres","flexgmres","lgmres","bicgstab"):
	for nlvl in range(1,9):
		for ph in range(0,5):
			for pk in range(0,5):
				name = NAME % { "nlvl": nlvl, "solver": solver, "ph": ph, "pk": pk };
				patches = 2**(ph)
				spacing = 2**(-ph)
				delt = 2**(-pk)
				maxt = 100 + delt

				ups = open(name+".ups", "w")
				ups.write(UPS % { "title": name, "delt": delt, "maxt": maxt, "patches": patches, "spacing": spacing, 'nlvl': nlvl, 'name': name, "solver": solver})
				ups.close()
