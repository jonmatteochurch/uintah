UPS = """
<Uintah_specification>
  <Meta>
    <title>%(title)s</title>
  </Meta>
  <SimulationComponent type="phasefield"/>
  <PhaseField type="heat_test">
    <var>cc</var>
    <dim>2</dim>
    <delt>%(delt)f</delt>
    <alpha>1.</alpha>
    <refine_threshold>0.5</refine_threshold>
    <scheme>backward_euler</scheme>
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
        <upper>[ 64., 64.,  1.]</upper>
        <patches>[%(patches)d,%(patches)d,1]</patches>
      </Box>
      <spacing>[%(spacing)f,%(spacing)f,1]</spacing>
    </Level>
    <BoundaryConditions>
      <Face side="x-">
        <BCType id="0" label="u" var="Neumann">
          <value>0.</value>
        </BCType>
        <BCType id="0" label="ux" var="Dirichlet">
          <value>0.</value>
        </BCType>
        <BCType id="0" label="uy" var="Dirichlet">
          <value>0.</value>
        </BCType>
        <BCType id="0" label="uxx" var="Neumann">
          <value>0.</value>
        </BCType>
        <BCType id="0" label="uyy" var="Neumann">
          <value>0.</value>
        </BCType>
      </Face>
      <Face side="y-">
        <BCType id="0" label="u" var="Neumann">
          <value>0.</value>
        </BCType>
        <BCType id="0" label="ux" var="Dirichlet">
          <value>0.</value>
        </BCType>
        <BCType id="0" label="uy" var="Dirichlet">
          <value>0.</value>
        </BCType>
        <BCType id="0" label="uxx" var="Neumann">
          <value>0.</value>
        </BCType>
        <BCType id="0" label="uyy" var="Neumann">
          <value>0.</value>
        </BCType>
      </Face>
      <Face side="x+">
        <BCType id="0" label="u" var="Dirichlet">
          <value>0.</value>
        </BCType>
        <BCType id="0" label="ux" var="Neumann">
          <value>0.</value>
        </BCType>
        <BCType id="0" label="uy" var="Neumann">
          <value>0.</value>
        </BCType>
        <BCType id="0" label="uxx" var="Dirichlet">
          <value>0.</value>
        </BCType>
        <BCType id="0" label="uyy" var="Dirichlet">
          <value>-0.65</value>
        </BCType>
      </Face>
      <Face side="y+">
        <BCType id="0" label="u" var="Dirichlet">
          <value>0.</value>
        </BCType>
        <BCType id="0" label="ux" var="Neumann">
          <value>0.</value>
        </BCType>
        <BCType id="0" label="uy" var="Neumann">
          <value>0.</value>
        </BCType>
        <BCType id="0" label="uxx" var="Dirichlet">
          <value>0.</value>
        </BCType>
        <BCType id="0" label="uyy" var="Dirichlet">
          <value>-0.65</value>
        </BCType>
      </Face>
    </BoundaryConditions>
  </Grid>
  <AMR>
    <Regridder type="Tiled">
      <adaptive>true</adaptive>
      <max_levels>%(nlvl)d</max_levels>
      <min_patch_size>[[16,16,1]]</min_patch_size>
      <min_boundary_cells>[1,1,0]</min_boundary_cells>
      <cell_refinement_ratio>[[2,2,1]]</cell_refinement_ratio>
      <cell_stability_dilation>[1,1,0]</cell_stability_dilation>
    </Regridder>
    <FineCoarseInterfaces>
      <FCIType id="1" label="u" var="FC1New"/>
      <FCIType id="1" label="ux" var="FC1New"/>
      <FCIType id="1" label="uy" var="FC1New"/>
      <FCIType id="1" label="uxx" var="FC1New"/>
      <FCIType id="1" label="uyy" var="FC1New"/>
    </FineCoarseInterfaces>
  </AMR>
  <Solver type="hypre">
    <Parameters variable="u">
      <solver>%(solver)s</solver>
      <preconditioner>diagonal</preconditioner>
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

NAME = "be_struct_conv_%(solver)s_2d_nlvl%(nlvl)1d_%(ph)d_%(pk)d"

MPSZ = {}
MPSZ[1] = "[[1,1,1],[2,2,1],[4,4,1],[8,8,1],[16,16,1]]"
MPSZ[2] = "[[2,2,1],[4,4,1],[8,8,1],[16,16,1]]"
MPSZ[4] = "[[4,4,1],[8,8,1],[16,16,1]]"
MPSZ[8] = "[[8,8,1],[16,16,1]]"
MPSZ[16] = "[[16,16,1]]"

for solver in ("pfmg","smg","cycred","gmres","flexgmres","lgmres","bicgstab"):
	for nlvl in range(1,8):
		for ph in range(0,5):
			for pk in range(0,5):
				name = NAME % { "nlvl": nlvl, "solver": solver, "ph": ph, "pk": pk };
				spacing = 2**(nlvl-ph-1)
				ncells = int(64/spacing)
				patches = max(1,ncells//16)
				ps = ncells//patches
				print { "patches": patches, "ncells": 64/spacing, "ps": ps, "spacing": spacing }
				if ps < 16:
					min_patch_size = MPSZ[ps]
				else:
					min_patch_size = MPSZ[16]
				delt = 2**(-pk)
				maxt = 100 + delt

				ups = open(name+".ups", "w")
				ups.write(UPS % { "title": name, "delt": delt, "maxt": maxt, "patches": patches, "spacing": spacing, 'nlvl': nlvl, 'min_patch_size': min_patch_size, 'name': name, "solver": solver})
				ups.close()
