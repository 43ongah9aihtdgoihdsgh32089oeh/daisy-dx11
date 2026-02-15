# daisy-dx11
special thanks to https://github.com/sse2/daisy
for the original DirectX9 daisy renderer
ported/rewritten to use DirectX11

# example usage
```
  // when creating rendertargetview
  daisy::daisy_set_viewport( viewport_width, viewport_height );

  if ( !daisy::daisy_initialize( device, context, viewport_width, viewport_height ) )
	  return;
  
  present_buffer = std::make_unique<c_single_buffer>( );
  present_buffer->queue.create( );

  default_font.create( "Segoe UI", 20 );
  initialized = true;

  void c_renderer::create_scene( )
  {
  	if ( !context || !render_target_view )
  		return;
  
  	context->OMSetRenderTargets( 1, &render_target_view, nullptr );
  	daisy::daisy_prepare( );
  	present_buffer->queue.clear( );
  }

  void c_renderer::end_scene( )
  {
  	if ( !context )
  		return;
  
  	present_buffer->queue.flush( );
  }

  present hook
  
  HRESULT present_scene::hook( IDXGISwapChain* swap_chain, UINT sync_interval, UINT flags )
  {
  	if ( !renderer->initialized )
  		renderer->init( swap_chain );
  
  	if ( !renderer->render_target_view )
  		renderer->create_render_target( );
  
  	renderer->create_scene( );
  	{
  		renderer->present_buffer->text( 200, 50, renderer->default_font, color( ), color( 0, 0, 0 ), true, "test text rendering\n post newline" );
  	}
  	renderer->end_scene( );
  
  	return original( swap_chain, sync_interval, flags );
  }
```
