/**
 * LightNVR Web Interface Users View Component
 * Preact component for the user management page
 */

import { useState, useEffect, useRef, useCallback } from 'preact/hooks';
import { showStatusMessage } from './UI.js';
import { useQuery, useMutation, fetchJSON } from '../../query-client.js';

// Import user components
import { USER_ROLES } from './users/UserRoles.js';
import { UsersTable } from './users/UsersTable.js';
import { AddUserModal } from './users/AddUserModal.js';
import { EditUserModal } from './users/EditUserModal.js';
import { DeleteUserModal } from './users/DeleteUserModal.js';
import { ApiKeyModal } from './users/ApiKeyModal.js';

/**
 * UsersView component
 * @returns {JSX.Element} UsersView component
 */
export function UsersView() {
  // State for modal visibility
  const [activeModal, setActiveModal] = useState(null); // 'add', 'edit', 'delete', 'apiKey', or null

  // State for selected user and API key
  const [selectedUser, setSelectedUser] = useState(null);
  const [apiKey, setApiKey] = useState('');

  // Form state for adding/editing users
  const [formData, setFormData] = useState({
    username: '',
    password: '',
    email: '',
    role: 1,
    is_active: true
  });

  /**
   * Get auth headers for requests
   * @returns {Object} Headers object with Authorization if available
   */
  const getAuthHeaders = useCallback(() => {
    const auth = localStorage.getItem('auth');
    return auth ? { 'Authorization': 'Basic ' + auth } : {};
  }, []);

  // Fetch users using useQuery
  const {
    data: usersData,
    isLoading: loading,
    error,
    refetch: refetchUsers
  } = useQuery(
    ['users'],
    '/api/auth/users',
    {
      headers: getAuthHeaders(),
      cache: 'no-store',
      timeout: 15000, // 15 second timeout
      retries: 2,     // Retry twice
      retryDelay: 1000 // 1 second between retries
    }
  );

  // Extract users array from response
  const users = usersData?.users || [];

  // Add event listener for the add user button
  useEffect(() => {
    const addUserBtn = document.getElementById('add-user-btn');
    if (addUserBtn) {
      const handleAddUserClick = () => {
        // Reset form data for new user
        setFormData({
          username: '',
          password: '',
          email: '',
          role: 1,
          is_active: true
        });
        setActiveModal('add');
      };

      addUserBtn.addEventListener('click', handleAddUserClick);

      return () => {
        if (addUserBtn) {
          addUserBtn.removeEventListener('click', handleAddUserClick);
        }
      };
    }
  }, []);

  /**
   * Handle form input changes
   * @param {Event} e - Input change event
   */
  const handleInputChange = useCallback((e) => {
    const { name, value, type, checked } = e.target;

    setFormData(prevData => ({
      ...prevData,
      [name]: type === 'checkbox' ? checked : (name === 'role' ? parseInt(value, 10) : value)
    }));
  }, []);

  // Add user mutation
  const addUserMutation = useMutation({
    mutationFn: async (userData) => {
      return await fetchJSON('/api/auth/users', {
        method: 'POST',
        headers: {
          'Content-Type': 'application/json',
          ...getAuthHeaders()
        },
        body: JSON.stringify(userData),
        timeout: 15000, // 15 second timeout
        retries: 1,     // Retry once
        retryDelay: 1000 // 1 second between retries
      });
    },
    onSuccess: () => {
      // Close modal and show success message
      setActiveModal(null);
      showStatusMessage('User added successfully', 'success', 5000);

      // Refresh users list
      refetchUsers();
    },
    onError: (error) => {
      console.error('Error adding user:', error);
      showStatusMessage(`Error adding user: ${error.message}`, 'error', 8000);
    }
  });

  // Edit user mutation
  const editUserMutation = useMutation({
    mutationFn: async ({ userId, userData }) => {
      return await fetchJSON(`/api/auth/users/${userId}`, {
        method: 'PUT',
        headers: {
          'Content-Type': 'application/json',
          ...getAuthHeaders()
        },
        body: JSON.stringify(userData),
        timeout: 15000, // 15 second timeout
        retries: 1,     // Retry once
        retryDelay: 1000 // 1 second between retries
      });
    },
    onSuccess: () => {
      // Close modal and show success message
      setActiveModal(null);
      showStatusMessage('User updated successfully', 'success', 5000);

      // Refresh users list
      refetchUsers();
    },
    onError: (error) => {
      console.error('Error updating user:', error);
      showStatusMessage(`Error updating user: ${error.message}`, 'error', 8000);
    }
  });

  // Delete user mutation
  const deleteUserMutation = useMutation({
    mutationFn: async (userId) => {
      return await fetchJSON(`/api/auth/users/${userId}`, {
        method: 'DELETE',
        headers: getAuthHeaders(),
        timeout: 15000, // 15 second timeout
        retries: 1,     // Retry once
        retryDelay: 1000 // 1 second between retries
      });
    },
    onSuccess: () => {
      // Close modal and show success message
      setActiveModal(null);
      showStatusMessage('User deleted successfully', 'success', 5000);

      // Refresh users list
      refetchUsers();
    },
    onError: (error) => {
      console.error('Error deleting user:', error);
      showStatusMessage(`Error deleting user: ${error.message}`, 'error', 8000);
    }
  });

  // Generate API key mutation
  const generateApiKeyMutation = useMutation({
    mutationFn: async (userId) => {
      return await fetchJSON(`/api/auth/users/${userId}/api-key`, {
        method: 'POST',
        headers: getAuthHeaders(),
        timeout: 20000, // 20 second timeout for key generation
        retries: 1,     // Retry once
        retryDelay: 2000 // 2 seconds between retries
      });
    },
    onMutate: () => {
      // Show loading state
      setApiKey('Generating...');
    },
    onSuccess: (data) => {
      // Set the API key and ensure the modal stays open
      setApiKey(data.api_key);
      showStatusMessage('API key generated successfully', 'success');

      // Refresh users list without affecting the modal
      // We'll use a separate function to avoid closing the modal
      setTimeout(() => {
        refetchUsers();
      }, 100);
    },
    onError: (error) => {
      console.error('Error generating API key:', error);
      setApiKey('');
      showStatusMessage(`Error generating API key: ${error.message}`, 'error');
    }
  });

  /**
   * Handle form submission for adding a user
   * @param {Event} e - Form submit event
   */
  const handleAddUser = useCallback((e) => {
    if (e) e.preventDefault();

    console.log('Adding user:', formData.username);
    addUserMutation.mutate(formData);
  }, [formData]);

  /**
   * Handle form submission for editing a user
   * @param {Event} e - Form submit event
   */
  const handleEditUser = useCallback((e) => {
    if (e) e.preventDefault();

    console.log('Editing user:', selectedUser.id, selectedUser.username);
    editUserMutation.mutate({
      userId: selectedUser.id,
      userData: formData
    });
  }, [selectedUser, formData]);

  /**
   * Handle user deletion
   */
  const handleDeleteUser = useCallback(() => {
    console.log('Deleting user:', selectedUser.id, selectedUser.username);
    deleteUserMutation.mutate(selectedUser.id);
  }, [selectedUser]);

  /**
   * Handle generating a new API key for a user
   */
  const handleGenerateApiKey = useCallback(() => {
    console.log('Generating API key for user:', selectedUser.id, selectedUser.username);
    generateApiKeyMutation.mutate(selectedUser.id);
  }, [selectedUser]);

  /**
   * Copy API key to clipboard
   */
  const copyApiKey = useCallback(() => {
    navigator.clipboard.writeText(apiKey)
      .then(() => {
        // Use global toast function if available
        if (window.showSuccessToast) {
          window.showSuccessToast('API key copied to clipboard');
        } else {
          // Fallback to standard showStatusMessage
          showStatusMessage('API key copied to clipboard', 'success');
        }
      })
      .catch((err) => {
        console.error('Error copying API key:', err);

        // Use global toast function if available
        if (window.showErrorToast) {
          window.showErrorToast('Failed to copy API key');
        } else {
          // Fallback to standard showStatusMessage
          showStatusMessage('Failed to copy API key', 'error');
        }
      });
  }, [apiKey]);

  /**
   * Open the edit modal for a user
   * @param {Object} user - User to edit
   */
  const openEditModal = useCallback((user) => {
    setSelectedUser(user);
    setFormData({
      username: user.username,
      password: '', // Don't include the password in the form
      email: user.email || '',
      role: user.role,
      is_active: user.is_active
    });
    setActiveModal('edit');
  }, []);

  /**
   * Open the delete modal for a user
   * @param {Object} user - User to delete
   */
  const openDeleteModal = useCallback((user) => {
    setSelectedUser(user);
    setActiveModal('delete');
  }, []);

  /**
   * Open the API key modal for a user
   * @param {Object} user - User to generate API key for
   */
  const openApiKeyModal = useCallback((user) => {
    setSelectedUser(user);
    setApiKey('');
    setActiveModal('apiKey');
  }, []);

  /**
   * Close any open modal
   */
  const closeModal = useCallback(() => {
    setActiveModal(null);
  }, []);

  // Render loading state
  if (loading && users.length === 0) {
    return (
      <div className="flex justify-center items-center p-8">
        <div className="w-12 h-12 border-4 border-blue-600 border-t-transparent rounded-full animate-spin"></div>
        <span className="sr-only">Loading...</span>
      </div>
    );
  }

  // Render empty state
  if (users.length === 0 && !loading) {
    return (
      <div>
        <div className="bg-blue-100 border border-blue-400 text-blue-700 px-4 py-3 rounded relative mb-4">
          <h4 className="font-bold mb-2">No Users Found</h4>
          <p>Click the "Add User" button to create your first user.</p>
        </div>
        {activeModal === 'add' && (
          <AddUserModal
            formData={formData}
            handleInputChange={handleInputChange}
            handleAddUser={handleAddUser}
            onClose={closeModal}
          />
        )}
      </div>
    );
  }

  // Render users table with modals
  return (
    <div>
      <UsersTable
        users={users}
        onEdit={openEditModal}
        onDelete={openDeleteModal}
        onApiKey={openApiKeyModal}
      />

      {activeModal === 'add' && (
        <AddUserModal
          formData={formData}
          handleInputChange={handleInputChange}
          handleAddUser={handleAddUser}
          onClose={closeModal}
        />
      )}

      {activeModal === 'edit' && (
        <EditUserModal
          currentUser={selectedUser}
          formData={formData}
          handleInputChange={handleInputChange}
          handleEditUser={handleEditUser}
          onClose={closeModal}
        />
      )}

      {activeModal === 'delete' && (
        <DeleteUserModal
          currentUser={selectedUser}
          handleDeleteUser={handleDeleteUser}
          onClose={closeModal}
        />
      )}

      {activeModal === 'apiKey' && (
        <ApiKeyModal
          currentUser={selectedUser}
          newApiKey={apiKey}
          handleGenerateApiKey={handleGenerateApiKey}
          copyApiKey={copyApiKey}
          onClose={closeModal}
        />
      )}
    </div>
  );
}
